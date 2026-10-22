/* SPDX-License-Identifier: MPL-2.0 */
/* The engine offline, from a few commands, into a WAV (for listening and the tools):
 *   lg_render OUT.wav pat N SECONDS [CUT_AT]      pattern N, cut (收) at CUT_AT s (default: never)
 *   lg_render OUT.wav bs N SECONDS CUE_AT,... END_AT    板式 N: 小过门 cued at those times, the ending at END_AT
 *   lg_render OUT.wav tq N SECONDS [CUT_AT]       唱段 N (tq_data.h), cut (收) at CUT_AT s (default: never)
 *   lg_render OUT.wav say N SECONDS               the 彩蛋's clip N alone (guo_snd.h): when it ended, its peak
 * Prints the events as they sound (the transport's log). SOLO=k keeps only mix row k (LV_GU .. LV_KEYS: 4 京胡,
 * 5 京二胡, 6 月琴); QUIET=1 drops the log. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lg.h"
#include "wav.h"

extern void (*lg_log)(const char *what, const char *detail, float tick);
static float now_s;
static int quiet;
static void log_fn(const char *what, const char *detail, float tick)
{
    if (!quiet)
        printf("%7.3f %-7s %s (tick %.1f)\n", (double)now_s, what, detail, (double)tick);
}

int main(int argc, char **argv)
{
    wav_t w;
    int32_t lr[512];
    float f[512];
    uint32_t at = 0, n, i, serial = 0;
    float secs, cut_at = -1.0f, end_at = -1.0f, cues[16];
    int ncue = 0, idx, bs, tq, say;
    float say_end = -1.0f, peak = 0.0f;
    if (argc < 5) {
        fprintf(stderr, "usage: lg_render OUT.wav pat|bs|tq|say N SECONDS [...]\n");
        return 2;
    }
    lg_init();
    lg_log = log_fn;
    quiet = getenv("QUIET") != 0;
    bs = !strcmp(argv[2], "bs");
    tq = !strcmp(argv[2], "tq");
    say = !strcmp(argv[2], "say");
    idx = atoi(argv[3]);
    secs = (float)atof(argv[4]);
    if (!bs && argc > 5)
        cut_at = (float)atof(argv[5]);
    if (bs && argc > 6) {
        char *s = argv[5];
        while (*s && ncue < 16) {
            cues[ncue++] = (float)strtod(s, &s);
            if (*s == ',')
                s++;
        }
        end_at = (float)atof(argv[6]);
    }
    if (getenv("SOLO")) {
        int k, solo = atoi(getenv("SOLO"));
        for (k = 0; k < LV_N; k++)
            if (k != solo)
                lg_set.vol[k] = 0;
    }
    if (tq && idx >= lg_ntq()) {
        fprintf(stderr, "no 唱段 %d (%d here)\n", idx, lg_ntq());
        return 2;
    }
    if (say && idx >= lg_nsay()) {
        fprintf(stderr, "no clip %d (%d here)\n", idx, lg_nsay());
        return 2;
    }
    if (!say)
        lg_set.bpm = tq ? lg_tq_sec(lg_tq(idx)->s0)->bpm : bs ? lg_bs(idx)->bpm : lg_pat(idx)->bpm;
    lg_cmd(say ? LG_SAY : tq ? LG_PLAY_TQ : bs ? LG_PLAY_BS : LG_PLAY_PAT, (uint32_t)idx, 0, 0);
    if (wav_open(&w, argv[1], 2))
        return 1;
    n = (uint32_t)(secs * 44100.0f);
    for (at = 0; at < n; at += 256u) {
        float t = (float)at / 44100.0f;
        int k;
        if (cut_at >= 0.0f && t >= cut_at) {
            lg_cmd(LG_CUT, 0, 0, 0);
            cut_at = -1.0f;
        }
        for (k = 0; k < ncue; k++)
            if (cues[k] >= 0.0f && t >= cues[k]) {
                lg_cmd(LG_CUE, 0, 0, 0);
                cues[k] = -1.0f;
            }
        if (end_at >= 0.0f && t >= end_at) {
            lg_cmd(LG_CUT, 0, 0, 0);
            end_at = -1.0f;
        }
        lg_time((uint32_t)(t * 1000.0f));
        now_s = t;
        lg_render(lr, 256u, 4096u);
        for (i = 0; i < 512u; i++) {
            f[i] = (float)lr[i] * (1.0f / 8388608.0f);
            peak = f[i] > peak ? f[i] : -f[i] > peak ? -f[i] : peak;
        }
        if (say && say_end < 0.0f && lg_now.say < 0)
            say_end = t;
        wav_put(&w, f, 256u);
        if (lg_now.serial != serial) {
            serial = lg_now.serial;
            if (lg_now.state != LG_IDLE && lg_now.ev >= 0 && lg_now.seg != SEG_GM)
                ;
        }
    }
    wav_close(&w);
    if (say)
        printf("say ended at %.2f s, peak %.3f\n", (double)say_end, (double)peak);
    printf("state %d seg %d at the end\n", lg_now.state, lg_now.seg);
    return 0;
}
