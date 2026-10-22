/* SPDX-License-Identifier: MPL-2.0 */
/* LUOGU's engine (lg.h): the transport, the patterns' strokes and the 过门's notes on time, the mix and the room.
 * Everything that sounds happens in the audio interrupt, in steps of LG_STEP frames (0.73 ms): the main loop only
 * queues commands (lg_cmd) and judges the practice (lg_service). One compilation unit, -O2. */
#include "lg.h"
#include "fastmath.h"
#include "lg_data.h"                            /* LG_SYL, LG_PAT, LG_GM, LG_BS (tools/lg_data.py) */
#if __has_include("tq_data.h")
#include "tq_data.h"                            /* the 唱段 (tools/tq_data.py: the owner's own, not LUOGU's) */
#endif
#ifndef TQ_N
#define TQ_N 0
#endif
#if __has_include("guo_snd.h")
#include "guo_snd.h"                            /* the 彩蛋's 包 and 肉包 (tools/guo.py: the owner's own, not LUOGU's) */
#endif
#ifndef GUO_NSND
#define GUO_NSND 0
#endif

#if defined(LG_HOST) || defined(PLAT_HOST)
#define LG_POOL
#else
#define LG_POOL __attribute__((section(".pool")))
#endif

/* the 京胡's open inner string, by 声腔: the scale degree it is tuned to, in semitones from 1 (西皮 and 南梆子 6̣,
 * 二黄 and 四平调 5̣, 反二黄 1); the string sits between G4 and E5, whatever the 调门 (京胡定弦速查表) */
static const int LG_INNER[LG_NQIANG] = {-3, -5, 0, -5, -3};
/* the instruments' levels in the mix (perc_tab.h's and samp_tab.h's are each its own): 板鼓 板 大锣 铙钹 小锣 堂鼓,
 * set so their open strokes' loudest 50 ms (K-weighted) sit 4, 7, 0, 1, 4 and 5 dB under the 大锣's (measured on
 * tests/host/perc_strokes after the 大锣 became a 虎音锣 and the others recordings); a 过门 (hu_tab.h) sits about
 * where its 锣鼓 lead-in does */
static const float LG_PERC_GAIN[I_NPERC] = {0.1132f, 0.1017f, 0.0844f, 0.0548f, 0.0482f, 0.2132f};
/* the output's limiter: its ceiling (-1 dBFS of 24 bits) and its release (100 ms), so the player's strokes on top of
 * a loud pattern duck the mix instead of clipping it */
#define LG_CEIL 7474000.0f
#define LG_REL 0.99977f

#define LG_STEP 32u                             /* frames between the transport's steps */
#define KARA_PEND ((int32_t)(1.0f * LG_SR))     /* karaoke: frames a key struck outside a pattern waits for its stroke */
#define KARA_QUIET ((int32_t)(0.04f * LG_SR))   /* karaoke: frames after a stroke struck when more keys are its chord */
#define LG_MAXN 512u                            /* frames a block at most */
#define LG_OUT_MS 6u                            /* a block rendered now is heard about this much later */

lg_set_t lg_set;
volatile lg_now_t lg_now;
volatile uint8_t lg_kara;
lg_score_t lg_score;

/* ------------------------------------------------------------------ the queues */
/* commands, main loop -> render: one word each (type, a, b, c a byte each) */
static volatile uint32_t cq[64];
static volatile uint32_t cq_w, cq_r;

void lg_cmd(uint32_t type, uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t w = cq_w;
    if (w - cq_r >= 64u)
        return;                                 /* (full: dropped; 64 between two blocks never happens) */
    cq[w & 63u] = (type & 255u) | (a & 255u) << 8 | (b & 255u) << 16 | (c & 255u) << 24;
    __asm__ volatile("" ::: "memory");
    cq_w = w + 1u;
}

/* the strokes the player owed (practice), render -> main loop: when each was heard (ms) */
static volatile uint32_t due_ms[64];
static volatile uint32_t due_w, due_r;
static uint32_t block_ms;                       /* the block's start (ms), from the app */
static uint32_t block_at;                       /* frames into the block */

void lg_time(uint32_t ms) { block_ms = ms; }

static void due_push(void)
{
    uint32_t w = due_w;
    if (w - due_r >= 64u)
        return;
    due_ms[w & 63u] = block_ms + LG_OUT_MS + (uint32_t)((float)block_at * (1000.0f / LG_SR));
    __asm__ volatile("" ::: "memory");
    due_w = w + 1u;
}

/* ------------------------------------------------------------------ the transport */
static float roll_end[I_NPERC];                 /* a roll's end (ticks into the segment; < 0 none) */
static struct {
    uint8_t seg;                                /* SEG_* */
    const lg_ev_t *ev;                          /* the segment's strokes */
    uint16_t nev, iev;
    const lg_note_t *nt;                        /* its notes (a 过门) */
    uint16_t nnt, int_;
    float len, tick;                            /* its length and where we are (ticks) */
    float acc0, acc1;                           /* 散板: the tempo's factor at its start and end */
    int8_t pat, gm, bs;
    uint8_t sec;
    uint8_t reps;                               /* how many times round the 中 (0: until cut) */
    uint8_t meter;
    uint8_t ending;                             /* singing: the ending is under way (tail, then end) */
    float beat_tick;                            /* ticks into the current beat (the 板眼's clock) */
    int32_t beat;
    uint8_t kara_go;                            /* karaoke: the player has struck the next stroke owed (lg_now.want) */
    uint8_t kara_a, kara_b;                     /* ... a key struck, to be judged (LG_KARA's a, b) */
    int32_t kara_pend;                          /* ... for this long (frames; in a pattern, till its next section) */
    int32_t kara_quiet;                         /* ... frames a struck stroke's chord (or bounce) is not judged */
    const lg_gm_t *gmp;                         /* the 过门 sounding (LG_GM's, or a 唱段's section) */
    int8_t tq;                                  /* the 唱段 under way (-1 none) */
    uint8_t tq_sec;                             /* ... its section, from its first */
    float tq_mul;                               /* ... its tempo against lg_set.bpm (the first section's): its 板式's */
} tr;

#ifdef LG_HOST
void (*lg_log)(const char *what, const char *detail, float tick);   /* the tools: what sounds, as it sounds */
#define LOG(w, d) (lg_log ? lg_log((w), (d), lg_log_tick) : (void)0)
#else
#define LOG(w, d) ((void)0)
#endif

static float lg_log_tick;

static void now_update(void)
{
    lg_now.seg = tr.seg;
    lg_now.pat = tr.pat;
    lg_now.gm = tr.gm;
    lg_now.sec = tr.sec;
    lg_now.meter = tr.meter;
    lg_now.serial++;
}

static void seg_pat(int p, int sec)             /* a pattern's section from its start */
{
    const lg_sec_t *s = &LG_PAT[p].s[sec];
    LOG(sec == LG_QI ? "qi" : sec == LG_ZHONG ? "zhong" : "shou", LG_PAT[p].name);
    tr.seg = SEG_PAT;
    tr.pat = (int8_t)p;
    tr.sec = (uint8_t)sec;
    tr.ev = s->ev;
    tr.nev = s->nev;
    tr.iev = 0;
    tr.nt = 0;
    tr.nnt = tr.int_ = 0;
    tr.len = (float)s->len;
    tr.tick = 0.0f;
    tr.meter = LG_PAT[p].meter;
    tr.acc0 = LG_PAT[p].acc0;
    tr.acc1 = LG_PAT[p].acc1;
    lg_now.ev = -1;
    lg_now.wait = 0;
    lg_now.want = -1;
    tr.kara_go = 0;
    now_update();
}

static void seg_gm_of(const lg_gm_t *m, int g)  /* a 过门 from its start (散: its tempo eases at the end); g: LG_GM's
                                                 * index, -1 a 唱段's section */
{
    LOG("guomen", m->name);
    tr.seg = SEG_GM;
    tr.gm = (int8_t)g;
    tr.gmp = m;
    tr.ev = m->pe;
    tr.nev = m->npe;
    tr.iev = 0;
    tr.nt = m->n;
    tr.nnt = m->nn;
    tr.int_ = 0;
    tr.len = (float)m->len;
    tr.tick = 0.0f;
    tr.meter = m->meter;
    tr.acc0 = 1.0f;
    tr.acc1 = m->meter ? 1.0f : 0.75f;
    lg_now.ev = -1;
    lg_now.note = -1;
    now_update();
}

static void seg_gm(int g) { seg_gm_of(&LG_GM[g], g); }

static const lg_ev_t NO_EV[1] = {{0, 0, 0, 0}};

static void seg_keep(const lg_bs_t *b)          /* singing: the 板眼 round and round (a pattern's 中), or the
                                                 * 京胡's 摇板 figure (紧拉慢唱), or, 散板, nothing: a beat at a time */
{
    if (b->keep_gm >= 0) {
        seg_gm(b->keep_gm);
    } else if (b->keep >= 0) {
        seg_pat(b->keep, LG_ZHONG);
    } else {
        tr.ev = NO_EV;
        tr.nev = 0;
        tr.iev = 0;
        tr.nt = 0;
        tr.nnt = tr.int_ = 0;
        tr.len = (float)LG_TPB;
        tr.tick = 0.0f;
        tr.meter = 0;
        tr.acc0 = tr.acc1 = 1.0f;
    }
    tr.seg = SEG_KEEP;
    lg_now.sing = 1;
    now_update();
}

static int lao_gm(const lg_bs_t *b, int pc)     /* the 小过门 for a 落音 (its pitch class from 1), -1 none */
{
    int i;
    for (i = 0; i < 6; i++)
        if (b->lao[i] >= 0 && ((LG_GM[b->lao[i]].lao % 12) + 12) % 12 == pc)
            return b->lao[i];
    return -1;
}

static void stop_all(void)
{
    int i;
    for (i = 0; i < I_NPERC; i++)
        if (roll_end[i] >= 0.0f) {
            roll_end[i] = -1.0f;
            pc_hit(i, PS_ROLLEND, 0.0f);
        }
    tr.seg = SEG_NONE;
    lg_now.state = LG_IDLE;
    lg_now.cut = lg_now.cue = 0;
    lg_now.note = -1;
    lg_now.wait = 0;
    lg_now.want = -1;
    tr.kara_go = 0;
    tr.kara_pend = tr.kara_quiet = 0;
    tr.tq = lg_now.tq = -1;
    tr.tq_mul = 1.0f;
    hu_off(HV_BAND_HU, 0.12f);
    hu_off(HV_BAND_ER, 0.12f);
    now_update();
}

/* a pattern's next section (or the end), when one ends */
static void pat_next(void)
{
    const lg_pat_t *p = &LG_PAT[tr.pat];
    int sec = tr.sec;
    if (sec == LG_QI) {
        lg_now.rep = 0;
        if (p->s[LG_ZHONG].len && !lg_now.cut) {
            seg_pat(tr.pat, LG_ZHONG);
            return;
        }
        sec = LG_ZHONG;
    }
    if (sec == LG_ZHONG) {
        lg_now.rep++;
        if (p->s[LG_ZHONG].len && !lg_now.cut && (!p->reps || lg_now.rep < p->reps)) {
            seg_pat(tr.pat, LG_ZHONG);
            return;
        }
        if (p->s[LG_SHOU].len) {
            seg_pat(tr.pat, LG_SHOU);
            return;
        }
    }
    stop_all();
}

/* singing: what follows a segment */
static int lead_in_next(void)                   /* singing: the lead-in's 中 once (or as often as it says), then
                                                 * its 收; 0 when it is done (or it is not the lead-in) */
{
    if (tr.seg == SEG_PAT && tr.sec != LG_SHOU && !tr.ending) {
        const lg_pat_t *p = &LG_PAT[tr.pat];
        if (tr.sec == LG_QI && p->s[LG_ZHONG].len) {
            lg_now.rep = 0;
            seg_pat(tr.pat, LG_ZHONG);
            return 1;
        }
        if (tr.sec == LG_ZHONG && ++lg_now.rep < (p->reps ? p->reps : 1)) {
            seg_pat(tr.pat, LG_ZHONG);
            return 1;
        }
        if (p->s[LG_SHOU].len) {
            seg_pat(tr.pat, LG_SHOU);
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ 唱段: an aria's 京胡 (its 托腔) from its 锣鼓 to
 * its 锣鼓, section by section, each a 过门 made here: its notes unpacked, its 板 and 眼 laid on its bars */
#if TQ_N
#define TQ_NSEC ((int)(sizeof TQ_SEC / sizeof TQ_SEC[0]))
static lg_note_t tq_nt[TQ_MAXNOTES];
static lg_ev_t tq_ev[TQ_MAXBEATS];
static lg_gm_t tq_gm;
static int8_t tq_bs_i[TQ_NSEC];                 /* each section's 板式 (LG_BS's), by its name; -1 none */
static uint8_t syl_ban, syl_yan;                /* the 板's stroke (扎) and the 眼's (多), as the 板眼 patterns' */

static float tq_mul_of(int bpm)                 /* a tempo against the 唱段's first section's (lg_set.bpm: K1) */
{
    return (float)bpm / (float)TQ_SEC[TQ_ARIA[tr.tq].s0].bpm;
}

static float tq_bs_mul(int s)                   /* section s's 锣鼓: at its 板式's own tempo, as K1 has moved it */
{
    int b = tq_bs_i[s];
    return b >= 0 ? tq_mul_of(LG_BS[b].bpm) : tq_mul_of(TQ_SEC[s].bpm);
}

static void seg_tq(int s)                       /* section s (TQ_SEC's) of the 唱段 under way, from its start */
{
    const lg_tq_sec_t *q = &TQ_SEC[s];
    const lg_tq_t *a = &TQ_ARIA[tr.tq];
    const uint8_t *b = TQ_NOTES + q->off;
    uint32_t i, at = 0, beats = q->len / LG_TPB, phase = (q->meter - q->pick % q->meter) % q->meter;
    for (i = 0; i < q->nn && i < TQ_MAXNOTES; i++) {   /* the notes (tools/tq_data.py's packing) */
        lg_note_t *n = &tq_nt[i];
        uint32_t b0 = *b++, b1 = *b++, d = b1 & 63u;
        n->at = (uint16_t)at;
        n->p = (int8_t)((int)(b0 & 63u) - 32);
        n->g = 0;
        n->fl = (uint8_t)(((b0 & 63u) == 63u ? LGN_REST : 0) | ((b0 & 64u) ? LGN_SLUR : 0) |
                          ((b1 & 64u) ? LGN_TRILL : 0));
        if (b0 & 128u) {
            n->g = (int8_t)((int)*b++ - 32);
            n->fl |= LGN_GRACE;
        }
        if (b1 & 128u)
            d |= (uint32_t)*b++ << 6;
        n->len = (uint16_t)(d * 6u);
        at += d * 6u;
    }
    for (i = 0; i < beats && i < TQ_MAXBEATS; i++) {   /* the 板 on each bar's first beat (a pickup's: a 眼), 眼 on the rest */
        int ban = (i + phase) % q->meter == 0;
        tq_ev[i].at = (uint16_t)(i * LG_TPB);
        tq_ev[i].syl = ban ? syl_ban : syl_yan;
        tq_ev[i].fl = (uint8_t)(LGF_BEAT | (ban ? LGF_BAR : LGF_EYE));
        tq_ev[i].len = 0;
    }
    tq_gm.name = q->bs;
    tq_gm.qiang = a->qiang;
    tq_gm.meter = q->meter;
    tq_gm.len = q->len;
    tq_gm.n = tq_nt;
    tq_gm.nn = q->nn;
    tq_gm.pe = tq_ev;
    tq_gm.npe = (uint16_t)i;
    tq_gm.sing = s == a->s0 && a->nl ? (uint16_t)(TQ_SYL[TQ_LINE[a->l0].y0].at - q->at) : 0;   /* the first word */
    tq_gm.lao = LG_NOLAO;
    seg_gm_of(&tq_gm, -1);
    tr.tq_sec = (uint8_t)(s - a->s0);
    tr.tq_mul = tq_mul_of(q->bpm);
    tr.beat = (tr.beat / q->meter + 1) * q->meter + (int32_t)phase;   /* (the 板眼 lit from the right beat) */
    lg_now.tq_sec = tr.tq_sec;
    LOG("tq", q->bs);
}

static int tq_continues(void)                   /* the section ending is followed by another */
{
    return tr.tq >= 0 && !lg_now.cut && tr.tq_sec + 1 < TQ_ARIA[tr.tq].ns;
}

static void tq_next(void)                       /* the 唱段: what follows a segment */
{
    const lg_tq_t *a = &TQ_ARIA[tr.tq];
    int b;
    if (lead_in_next())
        return;
    if (tr.ending) {                            /* the closing 锣鼓 done */
        if (tr.seg == SEG_PAT)
            pat_next();
        else
            stop_all();
        return;
    }
    if (tr.seg == SEG_PAT && !lg_now.cut) {     /* the lead-in done: the 京胡 */
        seg_tq(a->s0);
        return;
    }
    if (tr.seg == SEG_GM && tq_continues()) {
        seg_tq(a->s0 + tr.tq_sec + 1);
        return;
    }
    tr.ending = 1;                              /* sung through (or cut): its last 板式's closing 锣鼓 */
    lg_now.cut = 0;
    b = tq_bs_i[a->s0 + tr.tq_sec];
    tr.tq_mul = tq_bs_mul(a->s0 + tr.tq_sec);
    if (b >= 0 && LG_BS[b].end >= 0)
        seg_pat(LG_BS[b].end, LG_QI);
    else
        stop_all();
}

static void tq_cut(void)                        /* PLAY in a section: it ends at its next 板 */
{
    const lg_tq_sec_t *q = &TQ_SEC[TQ_ARIA[tr.tq].s0 + tr.tq_sec];
    uint32_t phase = (q->meter - q->pick % q->meter) % q->meter, i = (uint32_t)(tr.tick / (float)LG_TPB) + 1u;
    while ((i + phase) % q->meter)
        i++;
    if ((float)(i * LG_TPB) < tr.len)
        tr.len = (float)(i * LG_TPB);
}

static int same_text(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void tq_init(void)                       /* the sections' 板式 found by name, the 板眼's strokes */
{
    int s, i;
    for (s = 0; s < TQ_NSEC; s++) {
        tq_bs_i[s] = -1;
        for (i = 0; i < LG_NBS; i++)
            if (same_text(LG_BS[i].name, TQ_SEC[s].bs))
                tq_bs_i[s] = (int8_t)i;
    }
    for (i = 0; i < LG_NSYL; i++) {
        if (same_text(LG_SYL[i].zi, "扎"))
            syl_ban = (uint8_t)i;
        if (same_text(LG_SYL[i].zi, "多"))
            syl_yan = (uint8_t)i;
    }
}
#else
static void tq_next(void) { stop_all(); }
static int tq_continues(void) { return 0; }
static void tq_cut(void) {}
static void tq_init(void) {}
#endif

static void sing_next(void)
{
    const lg_bs_t *b = &LG_BS[tr.bs];
    if (tr.tq >= 0) {
        tq_next();
        return;
    }
    if (lead_in_next())
        return;
    if (tr.ending) {                            /* the tail 过门 done: the closing 锣鼓, then the end */
        if (tr.seg == SEG_GM && b->end >= 0) {
            seg_pat(b->end, LG_QI);
            return;
        }
        if (tr.seg == SEG_PAT) {
            pat_next();
            return;
        }
        stop_all();
        return;
    }
    if (tr.seg == SEG_PAT) {                    /* the lead-in done: the 大过门 */
        if (b->big >= 0) {
            seg_gm(b->big);
            return;
        }
        seg_keep(b);
        return;
    }
    if (tr.seg == SEG_GM) {                     /* a 过门 done: sing */
        seg_keep(b);
        return;
    }
    /* the keep's bar done: round again, unless a 小过门 or the ending was asked for */
    if (lg_now.cut) {
        tr.ending = 1;
        lg_now.cut = 0;
        lg_now.sing = 0;
        if (b->tail >= 0)
            seg_gm(b->tail);
        else if (b->end >= 0)
            seg_pat(b->end, LG_QI);
        else
            stop_all();
        return;
    }
    if (lg_now.cue) {                           /* the 小过门 for the player's 落音, else 上句's and 下句's in turn */
        int g = lg_now.cue_pc >= 0 ? lao_gm(b, lg_now.cue_pc) : -1;
        if (g < 0)
            g = b->small[lg_now.half & 1u];
        lg_now.cue = 0;
        lg_now.half ^= 1u;
        if (g >= 0) {
            if (b->gong)                        /* 散板: a stroke of the 大锣 between the lines */
                pc_hit(I_DALUO, PS_OPEN, 0.85f);
            lg_now.sing = 0;
            seg_gm(g);
            return;
        }
    }
    seg_keep(b);
}

static void play_pat(int p)
{
    if (p < 0 || p >= LG_NPAT)
        return;
    lg_now.state = LG_PATTERN;
    lg_now.cut = lg_now.cue = 0;
    lg_now.rep = 0;
    lg_now.bs = -1;
    lg_now.sing = 0;
    tr.bs = -1;
    tr.ending = 0;
    tr.kara_pend = tr.kara_quiet = 0;
    tr.tq = lg_now.tq = -1;
    tr.tq_mul = 1.0f;
    tr.beat = 0;
    tr.beat_tick = 0.0f;
    seg_pat(p, LG_QI);
    if (!LG_PAT[p].s[LG_QI].len)
        pat_next();
}

static void play_bs(int b)
{
    const lg_bs_t *s;
    if (b < 0 || b >= LG_NBS)
        return;
    s = &LG_BS[b];
    lg_now.state = LG_SINGING;
    lg_now.cut = lg_now.cue = 0;
    lg_now.half = 0;
    lg_now.rep = 0;
    lg_now.bs = (int8_t)b;
    tr.bs = (int8_t)b;
    tr.ending = 0;
    tr.kara_pend = tr.kara_quiet = 0;
    tr.tq = lg_now.tq = -1;
    tr.tq_mul = 1.0f;
    tr.beat = 0;
    tr.beat_tick = 0.0f;
    lg_now.sing = 0;
    if (s->intro >= 0)
        seg_pat(s->intro, LG_QI);
    else if (s->big >= 0)
        seg_gm(s->big);
    else
        seg_keep(s);
}

static void play_tq(int t)                      /* a 唱段: its first 板式's 锣鼓, then its 京胡 */
{
#if TQ_N
    const lg_tq_t *a;
    int b;
    if (t < 0 || t >= TQ_N)
        return;
    a = &TQ_ARIA[t];
    b = tq_bs_i[a->s0];
    lg_now.state = LG_SINGING;
    lg_now.cut = lg_now.cue = 0;
    lg_now.half = 0;
    lg_now.rep = 0;
    lg_now.bs = (int8_t)b;
    tr.bs = (int8_t)(b >= 0 ? b : 0);
    tr.tq = lg_now.tq = (int8_t)t;
    tr.tq_sec = lg_now.tq_sec = 0;
    lg_now.tq_at = 0;
    tr.tq_mul = tq_bs_mul(a->s0);
    tr.ending = 0;
    tr.kara_pend = tr.kara_quiet = 0;
    tr.beat = 0;
    tr.beat_tick = 0.0f;
    lg_now.sing = 0;
    if (b >= 0 && LG_BS[b].intro >= 0)
        seg_pat(LG_BS[b].intro, LG_QI);
    else
        seg_tq(a->s0);
#else
    (void)t;
#endif
}

/* ------------------------------------------------------------------ strokes */
static int mine_inst = -1;                      /* the player's instrument (practice), from lg_set.mine each block */

static void strike_syl(const lg_ev_t *e)
{
    const lg_syl_t *s = &LG_SYL[e->syl];
    uint32_t k;
    if ((e->fl & LGF_EYE) && !lg_set.yan)       /* a 眼's stroke, the 板 alone kept */
        return;
    LOG("stroke", s->zi);
    float acc = (e->fl & LGF_ACC) ? 1.15f : (e->fl & LGF_SOFT) ? 0.6f : 1.0f;
    for (k = 0; k < s->n && k < 4u; k++) {
        int inst = s->st[k] >> 4, st = s->st[k] & 15;
        float v = (float)s->vel[k] * (1.0f / 127.0f) * acc;
        if (inst == mine_inst || (inst == I_BAN && mine_inst == I_GU)) {   /* (the 鼓师 plays the 板 too) */
            if (st != PS_STOP && !(e->fl & LGF_FILL) && !lg_kara)   /* the player's: owed (what the 字谱 shows), not
                                                                     * played (karaoke: struck already) */
                due_push();
            continue;
        }
        if (e->fl & LGF_ROLL) {                 /* a roll (a tremolo) for the event's length */
            pc_hit(inst, PS_ROLL, v > 1.0f ? 1.0f : v);
            roll_end[inst] = (float)e->at + (float)e->len;
            continue;
        }
        if (st == PS_ROLL)                      /* a rolling syllable (嘟): till its length is up */
            roll_end[inst] = (float)e->at + (float)(e->len ? e->len : LG_TPB);
        pc_hit(inst, st, v > 1.0f ? 1.0f : v);
    }
}

/* ------------------------------------------------------------------ the 过门's notes */
static int inner_midi(int qiang)                /* the open inner string (MIDI): 67 (G4) .. 78 */
{
    int q = qiang < LG_NQIANG ? qiang : 0, pc = ((lg_set.key[q] + LG_INNER[q]) % 12 + 12) % 12;
    return 67 + ((pc - 7 + 12) % 12);
}

static int do_midi(int qiang)                   /* the 京胡's written 1 (do), as a MIDI note, in this 调门 */
{
    return inner_midi(qiang) - LG_INNER[qiang < LG_NQIANG ? qiang : 0];
}

int lg_do_midi(int qiang) { return do_midi(qiang); }

static int fold(int qiang, int m)               /* a note below the open string: an octave up, as a 琴师 plays it */
{
    int inner = inner_midi(qiang);
    while (m < inner)
        m += 12;
    return m;
}

static struct {                                 /* the 京胡's note under way: its ornaments still to come */
    float trill_at, trill_end;                  /* ticks: the trill's next change, its end */
    int8_t trill_p, trill_up;
    float grace_end;                            /* ticks: the grace note gives way to the note */
    float down_at;                              /* ticks: a falling slide starts */
    float end;                                  /* ticks: the note's end */
    int16_t idx;
    uint8_t on;
} nw;

static void hu_note(int voice, int midi, float vel, float slide_from, float slide_s, int legato, float vib)
{
    hu_note_t n;
    n.midi = (float)midi;
    n.vel = vel;
    n.slide_from = slide_from;
    n.slide_s = slide_s;
    n.vib = vib;
    n.vib_delay = 0.18f;
    n.legato = (uint8_t)legato;
    hu_on(voice, &n);
}

static float ticks_per_s(void)                  /* (a 唱段's section: at its 板式's tempo against the first's) */
{
    return (float)lg_set.bpm * tr.tq_mul * (float)LG_TPB / 60.0f;
}

static int prev_m = -1;                         /* the 京胡's last note (a slide into the next starts from it) */

static void note_start(int i)
{
    const lg_note_t *n = &tr.nt[i];
    int q = tr.gmp->qiang, dm = do_midi(q), m = fold(q, dm + n->p);
    float from = 0.0f;
    LOG("note", "");
    float tps = ticks_per_s(), len_s = (float)n->len / tps;
    int legato = (n->fl & LGN_SLUR) && nw.on;
    float vib = len_s > 0.35f ? 16.0f : 0.0f;
    nw.idx = (int16_t)i;
    nw.end = (float)(n->at + n->len);
    nw.grace_end = -1.0f;
    nw.down_at = -1.0f;
    nw.trill_at = nw.trill_end = -1.0f;
    if (n->fl & LGN_REST) {
        hu_off(HV_BAND_HU, 0.05f);
        hu_off(HV_BAND_ER, 0.06f);
        nw.on = 0;
        prev_m = -1;
        lg_now.note = (int16_t)i;
        lg_now.serial++;
        return;
    }
    if (n->fl & LGN_SLIDE_UP)                   /* a slide in: from the note before when it lies that way */
        from = prev_m >= 0 && prev_m < m && m - prev_m <= 5 ? (float)(prev_m - m) : -2.0f;
    else if (n->fl & LGN_SLIDE_DN)
        from = prev_m > m && prev_m - m <= 5 ? (float)(prev_m - m) : 2.0f;
    if (n->fl & LGN_GRACE) {                    /* the grace note first, quickly, then the note on the same bow */
        float gl = fm_minf(0.07f * tps, 0.3f * (float)n->len);
        hu_note(HV_BAND_HU, fold(q, dm + n->g), 0.8f, 0.0f, 0.0f, legato, 0.0f);
        nw.grace_end = (float)n->at + gl;
        legato = 1;
    } else {
        hu_note(HV_BAND_HU, m, 0.8f, from, from != 0.0f ? 0.11f : 0.0f, legato || from != 0.0f, vib);
    }
    prev_m = m;
    if (n->fl & LGN_SLIDE_DOWN)                 /* falls away at its end */
        nw.down_at = nw.end - fm_minf(0.12f * tps, 0.5f * (float)n->len);
    if (n->fl & LGN_TRILL) {
        nw.trill_p = (int8_t)m;
        nw.trill_up = 0;
        nw.trill_at = (float)n->at + 0.045f * tps;
        nw.trill_end = nw.end - 0.06f * tps;
    }
    if (lg_set.erhu) {                          /* the 京二胡: the same line an octave down, plainer */
        hu_note(HV_BAND_ER, m - 12, 0.7f, (n->fl & LGN_SLIDE_UP) ? -2.0f : 0.0f, 0.12f, legato && !(n->fl & LGN_GRACE),
                len_s > 0.4f ? 22.0f : 0.0f);
    }
    if (lg_set.yueqin && n->len >= LG_TPB / 2 && (n->at % (LG_TPB / 2)) == 0)
        yq_pluck((float)(m - 12), 0.75f);       /* the 月琴: the main notes, plucked */
    nw.on = 1;
    lg_now.note = (int16_t)i;
    lg_now.serial++;
}

static void notes_step(float t0, float t1)      /* the notes and ornaments due in [t0, t1) ticks */
{
    const lg_note_t *n;
    if (!tr.nt)
        return;
    while (tr.int_ < tr.nnt && (float)tr.nt[tr.int_].at < t1) {
        note_start(tr.int_);
        tr.int_++;
    }
    if (!nw.on)
        return;
    n = &tr.nt[nw.idx];
    if (nw.grace_end >= 0.0f && nw.grace_end < t1) {
        nw.grace_end = -1.0f;
        hu_note(HV_BAND_HU, fold(tr.gmp->qiang, do_midi(tr.gmp->qiang) + n->p), 0.8f, 0.0f, 0.0f, 1, 0.0f);
    }
    if (nw.trill_at >= 0.0f && nw.trill_at < t1) {
        if (nw.trill_at < nw.trill_end) {
            nw.trill_up ^= 1;
            hu_note(HV_BAND_HU, nw.trill_p + (nw.trill_up ? 2 : 0), 0.8f, 0.0f, 0.0f, 1, 0.0f);
            nw.trill_at += 0.045f * ticks_per_s();
        } else {
            nw.trill_at = -1.0f;
            hu_note(HV_BAND_HU, nw.trill_p, 0.8f, 0.0f, 0.0f, 1, 0.0f);
        }
    }
    if (nw.down_at >= 0.0f && nw.down_at < t1) {
        nw.down_at = -1.0f;
        hu_glide(HV_BAND_HU, (float)(fold(tr.gmp->qiang, do_midi(tr.gmp->qiang) + n->p) - 3),
                 (nw.end - t0) / ticks_per_s());
    }
    if (nw.end <= t1 && (tr.int_ >= tr.nnt || (float)tr.nt[tr.int_].at > nw.end + 0.5f)) {
        hu_off(HV_BAND_HU, 0.08f);              /* a gap follows: the bow lifts */
        hu_off(HV_BAND_ER, 0.10f);
        nw.on = 0;
        lg_now.note = -1;
        lg_now.serial++;
    }
}

/* karaoke: a key (LG_KARA's a, b) plays syllable syl, for a player of instrument mine (-1: all of them): with mine,
 * their instrument's stroke in it, struck its way (a light one the keys have no key for, the 铙钹's in 顷 or the
 * 堂鼓's: its open key, struck softly); else the syllable's own key, or the key of its one stroke (or of its first, if
 * the key is named for it: 匡 on the 大锣's key) */
int lg_kara_fits(int syl, uint32_t a, uint32_t b, int mine)
{
    const lg_syl_t *s;
    uint32_t k;
    if (syl < 0 || syl >= (int)LG_NSYL)
        return 0;
    s = &LG_SYL[syl];
    if (mine >= 0) {
        for (k = 0; k < s->n && k < 4u; k++) {
            int inst = s->st[k] >> 4;
            uint32_t st = s->st[k];
            if ((st & 15u) == PS_LIGHT && (inst == I_NAOBO || inst == I_TANGGU))   /* (app.c's keys: none light) */
                st = (uint32_t)inst << 4 | PS_OPEN;
            if ((inst == mine || (inst == I_BAN && mine == I_GU)) && st == b)
                return 1;
        }
        return 0;
    }
    if (b == 255u)
        return a == (uint32_t)syl;
    return s->n && (uint32_t)s->st[0] == b && (s->n == 1 || a == (uint32_t)syl);
}

/* karaoke: event e is the player's to strike, for a player of instrument mine (-1: all of them): every syllable, or
 * with mine those with their instrument (the 鼓师's with the 板 too); never the 鼓师's own fills, nor a hold */
int lg_kara_owes(const lg_ev_t *e, int mine)
{
    const lg_syl_t *s = &LG_SYL[e->syl];
    uint32_t k;
    if ((e->fl & (LGF_FILL | LGF_HOLD)) || !s->n)
        return 0;
    if (mine < 0)
        return 1;
    for (k = 0; k < s->n && k < 4u; k++) {
        int inst = s->st[k] >> 4;
        if (inst == mine || (inst == I_BAN && mine == I_GU))
            return 1;
    }
    return 0;
}

/* karaoke: event e of the pattern playing is the player's to strike */
static int kara_owed(const lg_ev_t *e) { return lg_kara && tr.seg == SEG_PAT && lg_kara_owes(e, mine_inst); }

/* karaoke: the next stroke owed in this section into lg_now.want (-1 none); 跟练 off (even while it waited: the
 * time goes on), none waited for and no key struck kept */
static void kara_next(void)
{
    int16_t w = -1;
    uint32_t i;
    if (lg_kara && tr.seg == SEG_PAT)
        for (i = tr.iev; i < tr.nev; i++)
            if (lg_kara_owes(&tr.ev[i], mine_inst)) {
                w = (int16_t)i;
                break;
            }
    if (!lg_kara)
        tr.kara_pend = 0;
    if (w != lg_now.want || (w < 0 && lg_now.wait)) {
        lg_now.want = w;
        if (w < 0)
            lg_now.wait = 0;
        lg_now.serial++;
    }
}

/* karaoke: the key struck (tr.kara_a, kara_b) against the next stroke owed (lg_now.want): its stroke (the time passes
 * it, or moves on to it), or a wrong one */
static void kara_judge(void)
{
    if (lg_kara_fits(tr.ev[lg_now.want].syl, tr.kara_a, tr.kara_b, mine_inst)) {
        tr.kara_go = 1;
        tr.kara_quiet = KARA_QUIET;
        lg_now.kara_hits++;
    } else {
        lg_now.kara_miss++;
    }
    tr.kara_pend = 0;
    lg_now.serial++;
}

/* ------------------------------------------------------------------ the step: the transport's next LG_STEP frames */
static void step(void)
{
    float tps, adv, t0, t1, f;
    if (tr.seg == SEG_NONE)
        return;
    kara_next();                                /* karaoke: a key struck, judged when a stroke is owed; one struck */
    if (tr.kara_pend > 0) {                     /* ahead of its time moves the time on to it (what lay between it */
        if (lg_now.want >= 0 && !tr.kara_go)    /* and the last: passed over, not struck) */
            kara_judge();
        else if (tr.seg != SEG_PAT)
            tr.kara_pend -= (int32_t)LG_STEP;
    }
    if (tr.kara_go && lg_now.want >= (int16_t)tr.iev && (float)tr.ev[lg_now.want].at > tr.tick) {
        float d = (float)tr.ev[lg_now.want].at - tr.tick;
        tr.tick += d;
        tr.beat_tick += d;
        tr.iev = (uint16_t)lg_now.want;
    }
    tps = ticks_per_s();
    f = 1.0f;
    if (tr.acc0 != 1.0f || tr.acc1 != 1.0f)     /* 散板: the tempo moves through the section */
        f = tr.acc0 + (tr.acc1 - tr.acc0) * (tr.len > 0.0f ? tr.tick / tr.len : 0.0f);
    adv = tps * f * (float)LG_STEP / LG_SR;
    t0 = tr.tick;
    t1 = t0 + adv;
    while (tr.iev < tr.nev && (float)tr.ev[tr.iev].at < t1) {   /* the strokes due */
        const lg_ev_t *e = &tr.ev[tr.iev];
        if (kara_owed(e)) {
            if (!tr.kara_go) {                  /* karaoke: the time stops at the stroke till the player strikes it */
                if (!lg_now.wait || lg_now.want != (int16_t)tr.iev) {
                    lg_now.wait = 1;
                    lg_now.want = (int16_t)tr.iev;
                    lg_now.serial++;
                }
                t1 = fm_maxf(t0, (float)e->at);
                adv = t1 - t0;
                break;
            }
            tr.kara_go = 0;
            lg_now.wait = 0;
            lg_now.want = -1;
            if (mine_inst >= 0 || (e->fl & LGF_ROLL))   /* the others' strokes with the player's (or the roll the
                                                         * player's stroke starts); all of it theirs: nothing more */
                strike_syl(e);
        } else {
            strike_syl(e);
        }
        if (!(e->fl & LGF_FILL)) {              /* (the screen lights the syllables, not the 鼓师's own strokes) */
            lg_now.ev = (int16_t)tr.iev;
            lg_now.serial++;
        }
        tr.iev++;
    }
    if (tr.kara_quiet > 0)                      /* karaoke: a struck stroke's chord, its time running out */
        tr.kara_quiet -= (int32_t)LG_STEP;
    {                                           /* rolls that are up */
        int i;
        for (i = 0; i < I_NPERC; i++)
            if (roll_end[i] >= 0.0f && roll_end[i] < t1) {
                roll_end[i] = -1.0f;
                pc_hit(i, PS_ROLLEND, 0.0f);
            }
    }
    notes_step(t0, t1);
    if (tr.seg == SEG_GM && !lg_now.sing && t1 >= (float)tr.gmp->sing && lg_now.state == LG_SINGING) {
        lg_now.sing = 1;                        /* the singer comes in here */
        lg_now.serial++;
    }
    tr.tick = t1;
    lg_log_tick = t1;
    tr.beat_tick += adv;
    while (tr.beat_tick >= (float)LG_TPB) {     /* the 板眼's clock */
        tr.beat_tick -= (float)LG_TPB;
        tr.beat++;
        lg_now.serial++;
    }
    lg_now.beat = tr.beat;
#if TQ_N
    if (tr.tq >= 0 && tr.seg == SEG_GM)         /* the 唱段's clock, for its lyrics */
        lg_now.tq_at = (uint16_t)(TQ_SEC[TQ_ARIA[tr.tq].s0 + tr.tq_sec].at + (uint32_t)tr.tick);
#endif
    lg_now.frac = tr.beat_tick * (1.0f / (float)LG_TPB);
    if (tr.tick >= tr.len) {                    /* the segment's end: the next one, the time carried over */
        float over = tr.tick - tr.len;
        if (tr.seg == SEG_GM && !tq_continues()) {   /* (a 唱段 going on into its next section: the bow too) */
            hu_off(HV_BAND_HU, 0.10f);
            hu_off(HV_BAND_ER, 0.12f);
            nw.on = 0;
        }
        {
            int i;
            for (i = 0; i < I_NPERC; i++)       /* (a roll running into the next segment: its end in its ticks) */
                if (roll_end[i] >= 0.0f)
                    roll_end[i] -= tr.len;
        }
        if (lg_now.state == LG_PATTERN)
            pat_next();
        else
            sing_next();
        if (tr.seg != SEG_NONE)
            tr.tick = over;
    }
}

/* ------------------------------------------------------------------ the 彩蛋: 郭德纲's 包 and 肉包 (guo_snd.h), a clip
 * at a time, its IMA ADPCM decoded as it plays and read at GUO_RATE, linearly between its samples */
int lg_nsay(void) { return GUO_NSND; }

#if GUO_NSND
static const int16_t IMA_STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107,
    118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358, 5894,
    6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767};
static const int8_t IMA_IDX[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
static struct {
    const uint8_t *p;                           /* the clip's ADPCM */
    uint32_t at, n;                             /* the next sample to decode, of n */
    int32_t pred, idx;                          /* the decoder */
    float s0, s1, frac, gain;                   /* the samples either side of where it reads, how far between */
} sy;

static float say_next(void)                     /* the clip's next sample (0 past its end) */
{
    int32_t code, step, diff;
    if (sy.at >= sy.n)
        return 0.0f;
    code = sy.p[sy.at >> 1] >> (4u * (sy.at & 1u)) & 15;
    sy.at++;
    step = IMA_STEP[sy.idx];
    diff = step >> 3;
    if (code & 4)
        diff += step;
    if (code & 2)
        diff += step >> 1;
    if (code & 1)
        diff += step >> 2;
    sy.pred += code & 8 ? -diff : diff;
    sy.pred = sy.pred > 32767 ? 32767 : sy.pred < -32768 ? -32768 : sy.pred;
    sy.idx += IMA_IDX[code];
    sy.idx = sy.idx > 88 ? 88 : sy.idx < 0 ? 0 : sy.idx;
    return (float)sy.pred * (1.0f / 32768.0f);
}

static void say_start(uint32_t a)
{
    if (a >= GUO_NSND)
        return;
    sy.p = GUO_ADPCM + GUO_SND[a].off;
    sy.n = GUO_SND[a].n;
    sy.gain = GUO_SND[a].gain;
    sy.at = 0;
    sy.pred = sy.idx = 0;
    sy.frac = 0.0f;
    sy.s0 = 0.0f;
    sy.s1 = say_next();
    lg_now.say = (int8_t)a;
}

static void say_render(float *l, float *r, uint32_t m)   /* (dry, both sides: it was one microphone) */
{
    uint32_t i;
    if (lg_now.say < 0)
        return;
    for (i = 0; i < m; i++) {
        float v = (sy.s0 + (sy.s1 - sy.s0) * sy.frac) * sy.gain;
        l[i] += v;
        r[i] += v;
        sy.frac += (float)GUO_RATE / LG_SR;
        while (sy.frac >= 1.0f) {
            sy.frac -= 1.0f;
            sy.s0 = sy.s1;
            sy.s1 = say_next();
        }
    }
    if (sy.at >= sy.n && sy.s0 == 0.0f && sy.s1 == 0.0f)
        lg_now.say = -1;
}
#endif

/* ------------------------------------------------------------------ commands */
static int keys_note = -1;                      /* the keys' voice: its note now */

static void command(uint32_t c)
{
    uint32_t type = c & 255u, a = c >> 8 & 255u, b = c >> 16 & 255u, v = c >> 24;
    switch (type) {
    case LG_PLAY_PAT:
        play_pat((int)a);
        break;
    case LG_PLAY_BS:
        play_bs((int)a);
        break;
    case LG_CUT:
        if (lg_now.state != LG_IDLE)
            lg_now.cut = 1;
        if (tr.tq >= 0 && tr.seg == SEG_GM)    /* a 唱段: at the next 板 */
            tq_cut();
        now_update();
        break;
    case LG_PLAY_TQ:
        play_tq((int)a);
        break;
    case LG_SAY:
#if GUO_NSND
        say_start(a);
#endif
        break;
    case LG_CUE:
        if (lg_now.state == LG_SINGING) {
            lg_now.cue = 1;
            lg_now.cue_pc = (int8_t)(a ? (int)a - 1 : -1);
        }
        now_update();
        break;
    case LG_STOP:
        stop_all();
        break;
    case LG_KARA:                               /* a key: judged by the next step (step: kara_judge); none while a
                                                 * struck stroke's chord or bounce may still come */
        if (lg_kara && tr.seg != SEG_NONE && !tr.kara_go && tr.kara_quiet <= 0) {
            tr.kara_a = (uint8_t)a;
            tr.kara_b = (uint8_t)b;
            tr.kara_pend = KARA_PEND;
        }
        break;
    case LG_HIT:
        if (a < I_NPERC)
            pc_hit((int)a, (int)b, (float)v * (1.0f / 127.0f));
        break;
    case LG_NOTE:                               /* the keys' voice: legato while a key is still down */
        if (lg_set.keys_inst == LG_KEYS_YUEQIN) {
            if (a && v)
                yq_pluck((float)a, (float)v * (1.0f / 127.0f));
        } else if (a && v) {
            hu_note_t n;
            hu_kind(HV_PLAYER, lg_set.keys_inst == LG_KEYS_ERHU ? HU_ERHU : HU_JINGHU);
            n.midi = (float)a;
            n.vel = (float)v * (1.0f / 127.0f);
            n.slide_from = 0.0f;
            n.slide_s = 0.0f;
            n.vib = lg_set.keys_inst == LG_KEYS_ERHU ? 28.0f : 16.0f;
            n.vib_delay = 0.2f;
            n.legato = (uint8_t)(keys_note >= 0);
            hu_on(HV_PLAYER, &n);
            keys_note = (int)a;
        } else if (!a || (int)a == keys_note) {
            hu_off(HV_PLAYER, 0.10f);
            keys_note = -1;
        }
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ the room */
#define RV_N 4
static const int RV_LEN[RV_N] = {1301, 1559, 1823, 2083};   /* (a hall's first reflections, prime lengths) */
static float rv_buf[1301 + 1559 + 1823 + 2083] LG_POOL;
static int rv_w[RV_N], rv_base[RV_N];
static float rv_lp[RV_N];

static inline void room_tick(float in, float *l, float *r, float fb)
{
    float o[RV_N], m[RV_N];
    int i;
    for (i = 0; i < RV_N; i++) {
        float x = rv_buf[rv_base[i] + rv_w[i]];
        rv_lp[i] += 0.42f * (x - rv_lp[i]);    /* the walls take the highs */
        o[i] = rv_lp[i];
    }
    m[0] = 0.5f * (o[0] + o[1] + o[2] + o[3]);
    m[1] = 0.5f * (o[0] - o[1] + o[2] - o[3]);
    m[2] = 0.5f * (o[0] + o[1] - o[2] - o[3]);
    m[3] = 0.5f * (o[0] - o[1] - o[2] + o[3]);
    for (i = 0; i < RV_N; i++) {
        rv_buf[rv_base[i] + rv_w[i]] = in + fb * m[i];
        if (++rv_w[i] >= RV_LEN[i])
            rv_w[i] = 0;
    }
    *l = o[0] + o[2];
    *r = o[1] + o[3];
}

/* ------------------------------------------------------------------ */
void lg_defaults(lg_set_t *s)
{
    static const int8_t KEY[LG_NQIANG] = {4, 4, 11, 4, 4};   /* 西皮 E, 二黄 E, 反二黄 B, 四平调 E, 南梆子 E: the
                                                              * common 调门 of 票友 (男; 女 二黄 D, 四平调 C) */
    int i;
    s->bpm = LG_PAT[0].bpm;
    for (i = 0; i < LG_NQIANG; i++)
        s->key[i] = KEY[i];
    for (i = 0; i < LV_N; i++)
        s->vol[i] = 80;
    s->room = 30;
    s->keys_inst = LG_KEYS_LUOGU;
    s->mine = 0;
    s->erhu = 1;
    s->yueqin = 1;
    s->yan = 0;
}

void lg_init(void)
{
    int i, b = 0;
    for (i = 0; i < I_NPERC; i++)
        roll_end[i] = -1.0f;
    pc_init();
    hu_init();
    for (i = 0; i < RV_N; i++) {
        rv_base[i] = b;
        b += RV_LEN[i];
    }
    lg_defaults(&lg_set);
    tr.tq = lg_now.tq = -1;
    tr.tq_mul = 1.0f;
    tr.gmp = &LG_GM[0];
    tq_init();
    lg_panic();
    lg_now.pat = 0;
    lg_now.bs = -1;
    lg_now.gm = -1;
    lg_now.ev = -1;
    lg_now.note = -1;
}

void lg_panic(void)
{
    uint32_t j;
    pc_panic();
    hu_panic();
    lg_now.say = -1;
    for (j = 0; j < sizeof rv_buf / sizeof rv_buf[0]; j++)
        rv_buf[j] = 0.0f;
    tr.seg = SEG_NONE;
    lg_now.state = LG_IDLE;
    nw.on = 0;
    keys_note = -1;
}

void lg_render(int32_t *lr, uint32_t n, uint32_t gain_q12)
{
    static float L[LG_MAXN], R[LG_MAXN], S[LG_MAXN], lim;
    float lv[I_NPERC], hv[HU_NVOICE], yv, gain = (float)gain_q12 * (1.0f / 4096.0f) * 8300000.0f;
    float fb = 0.62f + 0.28f * (float)lg_set.room * 0.01f, wet = 0.10f + 0.30f * (float)lg_set.room * 0.01f;
    uint32_t i, at;
    while (cq_r != cq_w) {
        command(cq[cq_r & 63u]);
        cq_r++;
    }
    mine_inst = lg_set.mine ? (int)lg_set.mine - 1 : -1;
    lv[I_GU] = lv[I_BAN] = lv[I_TANGGU] = (float)lg_set.vol[LV_GU] * 0.01f;
    lv[I_DALUO] = (float)lg_set.vol[LV_DALUO] * 0.01f;
    lv[I_NAOBO] = (float)lg_set.vol[LV_NAOBO] * 0.01f;
    lv[I_XIAOLUO] = (float)lg_set.vol[LV_XIAOLUO] * 0.01f;
    for (i = 0; i < I_NPERC; i++)
        lv[i] *= LG_PERC_GAIN[i];
    hv[HV_BAND_HU] = (float)lg_set.vol[LV_JINGHU] * 0.01f;
    hv[HV_BAND_ER] = (float)lg_set.vol[LV_ERHU] * 0.01f;
    hv[HV_PLAYER] = (float)lg_set.vol[LV_KEYS] * 0.01f;
    yv = (float)lg_set.vol[LV_YUEQIN] * 0.01f;
    if (n > LG_MAXN)
        n = LG_MAXN;
    for (i = 0; i < n; i++)
        L[i] = R[i] = S[i] = 0.0f;
    for (at = 0; at < n; at += LG_STEP) {
        uint32_t m = n - at < LG_STEP ? n - at : LG_STEP;
        block_at = at;
        step();
#ifndef NO_PERC
        pc_render(L + at, R + at, S + at, m, lv);
#endif
#ifndef NO_HU
        hu_render(L + at, R + at, S + at, m, hv, yv);
#endif
#if GUO_NSND
        say_render(L + at, R + at, m);
#endif
    }
    for (i = 0; i < n; i++) {
        float rl = 0.0f, rr = 0.0f, l, r, pk;
#ifndef NO_ROOM
        room_tick(S[i], &rl, &rr, fb);
#endif
        l = (L[i] + wet * rl) * gain;
        r = (R[i] + wet * rr) * gain;
        pk = l < 0.0f ? -l : l;
        if (r > pk || -r > pk)
            pk = r < 0.0f ? -r : r;
        lim = lim * LG_REL > pk ? lim * LG_REL : pk;
        if (lim > LG_CEIL) {
            float g = LG_CEIL / lim;
            l *= g;
            r *= g;
        }
        lr[2 * i] = (int32_t)l;
        lr[2 * i + 1] = (int32_t)r;
    }
}

/* ------------------------------------------------------------------ the data, for the screen */
const lg_syl_t *lg_syl(int i) { return &LG_SYL[i]; }
const lg_pat_t *lg_pat(int i) { return &LG_PAT[i]; }
const lg_gm_t *lg_gm(int i) { return &LG_GM[i]; }
const lg_gm_t *lg_cur_gm(void) { return tr.seg == SEG_GM ? tr.gmp : 0; }
#if TQ_N
int lg_ntq(void) { return TQ_N; }
const lg_tq_t *lg_tq(int i) { return &TQ_ARIA[i]; }
const lg_tq_sec_t *lg_tq_sec(int i) { return &TQ_SEC[i]; }
int lg_tq_bs(int i) { return tq_bs_i[i]; }
const lg_tq_line_t *lg_tq_line(int i) { return &TQ_LINE[i]; }
const lg_tq_syl_t *lg_tq_syl(int i) { return &TQ_SYL[i]; }
const uint8_t *lg_tq_glyph(int i) { return TQ_GLYPH[i]; }
int lg_tq_glyph_of(uint32_t code)
{
    int i;
    for (i = 0; i < TQ_NGLYPH; i++)
        if (TQ_GLYPH_CODE[i] == code)
            return i;
    return -1;
}
#else
int lg_ntq(void) { return 0; }
const lg_tq_t *lg_tq(int i) { (void)i; return 0; }
const lg_tq_sec_t *lg_tq_sec(int i) { (void)i; return 0; }
int lg_tq_bs(int i) { (void)i; return -1; }
const lg_tq_line_t *lg_tq_line(int i) { (void)i; return 0; }
const lg_tq_syl_t *lg_tq_syl(int i) { (void)i; return 0; }
const uint8_t *lg_tq_glyph(int i) { (void)i; return 0; }
int lg_tq_glyph_of(uint32_t code) { (void)code; return -1; }
#endif
const lg_bs_t *lg_bs(int i) { return &LG_BS[i]; }
const char *lg_cat_name(int i) { return LG_CAT_NAME[i]; }
int lg_nsyl(void) { return LG_NSYL; }
int lg_npat(void) { return LG_NPAT; }
int lg_ngm(void) { return LG_NGM; }
int lg_nbs(void) { return LG_NBS; }
int lg_ncat(void) { return LG_NCAT; }

/* ------------------------------------------------------------------ the practice's judging (main loop) */
#define J_WINDOW 150u                           /* ms: a stroke this far from one owed is for it */
#define J_GOOD_MS 45                            /* ms: on time */
static uint32_t owed[32];                       /* strokes owed, not yet struck (ms), oldest first */
static uint32_t nowed;
static uint32_t hits[16];                       /* the player's strokes not yet matched (ms) */
static uint32_t nhits;

void lg_score_reset(void)
{
    lg_score.due = lg_score.good = lg_score.early = lg_score.late = lg_score.miss = lg_score.extra = 0;
    lg_score.last = J_NONE;
    nowed = nhits = 0;
}

static void judge(uint8_t j, int32_t off, uint32_t at)
{
    lg_score.last = j;
    lg_score.last_ms = (int16_t)off;
    lg_score.last_at = at;
    if (j == J_GOOD)
        lg_score.good++;
    else if (j == J_EARLY)
        lg_score.early++;
    else if (j == J_LATE)
        lg_score.late++;
    else if (j == J_MISS)
        lg_score.miss++;
    else if (j == J_EXTRA)
        lg_score.extra++;
}

void lg_player_hit(int inst, uint32_t ms)
{
    (void)inst;
    if (nhits < 16u)
        hits[nhits++] = ms;
}

void lg_service(uint32_t now)
{
    uint32_t i, k;
    while (due_r != due_w) {                    /* the strokes owed, as the render reached them */
        if (nowed < 32u)
            owed[nowed++] = due_ms[due_r & 63u];
        lg_score.due++;
        due_r++;
    }
    for (i = 0; i < nhits;) {                   /* each stroke of the player's: the nearest owed one, if near */
        uint32_t h = hits[i], best = 0xFFFFFFFFu;
        int bi = -1;
        for (k = 0; k < nowed; k++) {
            uint32_t d = h > owed[k] ? h - owed[k] : owed[k] - h;
            if (d < best)
                best = d, bi = (int)k;
        }
        if (bi >= 0 && best <= J_WINDOW) {
            int32_t off = (int32_t)(h - owed[bi]);
            judge(off < -J_GOOD_MS ? J_EARLY : off > J_GOOD_MS ? J_LATE : J_GOOD, off, h);
            for (k = (uint32_t)bi; k + 1u < nowed; k++)
                owed[k] = owed[k + 1u];
            nowed--;
        } else if (bi < 0 || h + J_WINDOW < owed[bi] || (int32_t)(now - h) > (int32_t)J_WINDOW) {
            judge(J_EXTRA, 0, h);               /* nothing owed near it (and nothing coming) */
        } else {
            i++;                                /* one owed may still come within the window: wait */
            continue;
        }
        for (k = i; k + 1u < nhits; k++)
            hits[k] = hits[k + 1u];
        nhits--;
    }
    while (nowed && (int32_t)(now - owed[0]) > (int32_t)J_WINDOW) {   /* owed and never struck */
        judge(J_MISS, 0, owed[0]);
        for (k = 0; k + 1u < nowed; k++)
            owed[k] = owed[k + 1u];
        nowed--;
    }
}
