/* SPDX-License-Identifier: MPL-2.0 */
/* LUOGU's audio render, block by block, as Felucca tests its own (after TRIO's trio_perf): each block of BLOCK
 * frames (128: 2.9 ms) must be rendered in at most BUDGET % of its time, the rest left to the main loop. Counts the
 * instructions each lg_render call takes and turns them into an FM-1 core's share with the calibration from X0X's
 * PERF TEST (0.030 % a core per instruction a frame). Each scenario runs three times from the same state; each block
 * keeps its lowest count (what the Mac adds now and then is gone).
 *
 *   lg_perf [BLOCK (128)] [BUDGET % (60)]
 *
 * The scenarios stress it: every pattern at its fastest with the player's keys striking on top; a 板式 sung to with
 * the 京胡, the 京二胡 and the 月琴, the keys playing a 京胡 over it. Exit 1 when a block goes over the budget. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lg.h"
#ifdef __APPLE__
#include <libproc.h>
#include <unistd.h>
static unsigned long long insns(void)
{
    struct rusage_info_v4 ri;
    proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri);
    return ri.ri_instructions;
}
#else
static unsigned long long insns(void) { return 0; }
#endif

#define PCT_PER_INSN_FRAME 0.030
#define MAXBLK 40000

static unsigned long long cost[MAXBLK];

static unsigned long long overhead(void)
{
    unsigned long long best = ~0ull;
    int i;
    for (i = 0; i < 2000; i++) {
        unsigned long long a = insns(), b = insns();
        if (b - a < best)
            best = b - a;
    }
    return best;
}

/* one run: kind 0 a pattern (at its fastest, the keys striking a stroke every 90 ms), 1 a 板式 (the keys a 京胡 line),
 * 2 a 唱段 (at 1.3 times its tempo, the keys a 京胡 line; its first section unpacked in the run) */
static int run(int kind, int idx, float secs, unsigned block, int rep, unsigned long long ovh)
{
    int32_t lr[2 * 512];
    unsigned nblk = (unsigned)(secs * 44100.0f) / block, b;
    lg_init();
    lg_set.room = 100;
    lg_set.erhu = lg_set.yueqin = 1;
    if (kind == 0) {
        lg_set.bpm = lg_pat(idx)->hi;
        lg_cmd(LG_PLAY_PAT, (uint32_t)idx, 0, 0);
    } else if (kind == 1) {
        lg_set.bpm = lg_bs(idx)->hi;
        lg_set.keys_inst = LG_KEYS_JINGHU;
        lg_cmd(LG_PLAY_BS, (uint32_t)idx, 0, 0);
    } else {
        lg_set.bpm = (uint16_t)(lg_tq_sec(lg_tq(idx)->s0)->bpm * 13u / 10u);
        lg_set.keys_inst = LG_KEYS_JINGHU;
        lg_cmd(LG_PLAY_TQ, (uint32_t)idx, 0, 0);
    }
    for (b = 0; b < nblk && b < MAXBLK; b++) {
        unsigned long long a, z;
        float t = (float)(b * block) / 44100.0f;
        if (kind == 0 && (b * block) % 3969u < block)   /* a stroke of the player's every 90 ms, round the band */
            lg_cmd(LG_HIT, (b / 7u) % I_NPERC, PS_OPEN, 110);
        if (kind >= 1 && (b * block) % 11025u < block)   /* a note of the player's every 250 ms */
            lg_cmd(LG_NOTE, 64u + (b / 13u) % 12u, 0, 100);
        if (kind == 1 && t > secs * 0.5f && (b * block) % 44100u < block)
            lg_cmd(LG_CUE, 0, 0, 0);
        if (kind == 0 && t > secs * 0.8f && (b * block) % 44100u < block)
            lg_cmd(LG_CUT, 0, 0, 0);
        lg_time((uint32_t)(t * 1000.0f));
        a = insns();
        lg_render(lr, block, 4096);
        z = insns();
        z = z - a > ovh ? z - a - ovh : 0;
        if (rep == 0 || z < cost[b])
            cost[b] = z;
    }
    return (int)(nblk < MAXBLK ? nblk : MAXBLK);
}

int main(int argc, char **argv)
{
    unsigned block = argc > 1 ? (unsigned)atoi(argv[1]) : 128u;
    double budget = argc > 2 ? atof(argv[2]) : 60.0, worst_all = 0.0;
    unsigned long long ovh = overhead();
    int kind, idx, fail = 0;
    for (kind = 0; kind < 3; kind++) {
        int n = kind == 0 ? lg_npat() : kind == 1 ? lg_nbs() : lg_ntq();
        for (idx = 0; idx < n; idx++) {
            int rep, nb = 0, b, wb = 0;
            double sum = 0.0, worst = 0.0;
            for (rep = 0; rep < 3; rep++)
                nb = run(kind, idx, kind == 0 ? 12.0f : kind == 1 ? 30.0f : 40.0f, block, rep, ovh);
            for (b = 0; b < nb; b++) {
                double pct = (double)cost[b] / block * PCT_PER_INSN_FRAME;
                sum += pct;
                if (pct > worst)
                    worst = pct, wb = b;
            }
            printf("  %-12s %-14s mean %5.1f %%  worst block %5.1f %% (at %.2f s)\n",
                   kind == 2 ? "唱段" : kind ? "板式" : "pattern",
                   kind == 2 ? lg_tq(idx)->first : kind ? lg_bs(idx)->name : lg_pat(idx)->name, sum / nb, worst,
                   (double)(wb * block) / 44100.0);
            if (worst > worst_all)
                worst_all = worst;
            if (worst > budget)
                fail = 1;
        }
    }
    printf("worst block %.1f %% of a core (budget %.0f %%)%s\n", worst_all, budget, fail ? ": OVER" : "");
    return fail;
}
