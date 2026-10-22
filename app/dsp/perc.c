/* SPDX-License-Identifier: MPL-2.0 */
/* LUOGU's percussion (perc.h): modal synthesis. An instrument is its modes (two-pole resonators, each with the
 * frequency, level and ring measured in recordings: perc_tab.h, tools/perc_fit.py), bands of noise for the cymbals'
 * wash, and a burst of noise for the contact. A stroke adds to what already rings. A gong's pitch follows how hard it
 * rings: each mode sounds at hz * (1 + glide * a), a the ring's follower (1 just after a full open stroke), so the
 * big gong's pitch falls as it dies away and the small gong's rises, and both jump back at the next stroke.
 * The coefficients are updated every PC_CHUNK frames; strokes land on those boundaries (0.73 ms).
 * The instruments samp_tab.h has strokes for (the 单皮鼓, the 板, the 小锣, the 铙钹: tools/samples.py) play those
 * recordings instead: a voice a stroke. The 大锣 (a 虎音锣, whose pitch sinks as far as it was struck hard) and the
 * 堂鼓 stay modal. One compilation unit, -O2. No doubles, no libm (fastmath.h). */
#include "perc.h"
#include "fastmath.h"
#include "perc_tab.h"                           /* PC_INST[I_NPERC]: the instruments as measured */
#include "samp_tab.h"                           /* SAMP_SET[I_NPERC], SAMP, SAMP_DATA: the recorded strokes */

#if defined(LG_HOST) || defined(PLAT_HOST)
#define PC_POOL
#else
#define PC_POOL __attribute__((section(".pool")))
#endif

#define PC_MAXMODE 40
#define PC_MAXBAND 12
#define PC_CHUNK 32u
#define PC_QUIET 3e-5f                          /* below this an instrument rests (about -90 dB) */
#define PC_DEAD 6e-5f                           /* a mode this small (re. its stroke's 1) is gone: about -84 dB, and
                                                 * the instrument's level in the mix takes 25 dB more */


typedef struct {
    float y1[PC_MAXMODE], y2[PC_MAXMODE];       /* the resonators' state */
    float b1[PC_MAXMODE], b2[PC_MAXMODE];       /* a swelling mode's second resonator (its rise) */
    float rbloom[PC_MAXMODE];                   /* its decay a sample (0: the mode doesn't swell) */
    float bmix[PC_MAXMODE];                     /* (1 - direct): how much the rise takes away at first */
    float exn[PC_MAXMODE];                      /* the impulse that gives the mode its level at its peak */
    float c1[PC_MAXMODE], c2[PC_MAXMODE];       /* their coefficients, this chunk */
    float w0[PC_MAXMODE];                       /* their frequencies at rest (radians a sample) */
    float cw0[PC_MAXMODE], sw0[PC_MAXMODE];     /* ... their cosines and sines (a glide's cosine from them) */
    float rfree[PC_MAXMODE];                    /* their decay a sample, ringing free */
    float ex[PC_MAXMODE];                       /* a stroke's impulse, due at the next chunk */
    float benv[PC_MAXBAND], bmul[PC_MAXBAND], bfree[PC_MAXBAND];   /* the bands: level, decay, decay free */
    float bc1[PC_MAXBAND], bc2[PC_MAXBAND], bg[PC_MAXBAND], by1[PC_MAXBAND], by2[PC_MAXBAND];   /* their resonators:
                                                 * coefficients, the noise's gain (its peak at 1, as the SVF's
                                                 * bandpass had), state */
    float rmute, bmute;                         /* the decay a sample with the hand on it */
    float a, amul, as, asmul;                   /* the glide's drivers, fast and slow (the energy struck in, 1 after a full
                                                 * stroke), and their decays a chunk */
    float hold;                                 /* 1 ringing free, falling toward 0 with the hand on it */
    float click, cmul, clp, ck, chp, chk;       /* the contact: level, decay, its lowpass's and highpass's state and
                                                 * coefficients */
    float life, lmul, lmute;                    /* the slowest mode's level and its decay a chunk (free, the hand on it):
                                                 * when it is quiet the instrument rests */
    float gl, gr;                               /* the pan */
    float h1, h2, hk;                           /* the output's highpass: two one-pole lowpasses' states, coefficient */
    float n1, n2, nk;                           /* the bands' noise's lowpass: the same */
    float nx1, nx2;                             /* ... its last two samples (for its second difference) */
    float t1, t2, tk;                           /* ... that lowpassed again (tail_lp): the states, the coefficient */
    float bga[PC_MAXBAND];                      /* the bands' gains as all-pole resonators (the floor below them) */
    float bxf, bxmul;                           /* that floor's share: 1 at a stroke, dying in floor_t; its decay a
                                                 * chunk */
    float roll_vel, roll_left;                  /* a roll: its velocity, frames to its next stroke */
    uint32_t roll_n;
    uint8_t rolling, muted, awake;
} pc_t;
static pc_t pc[I_NPERC];
static const pc_inst_t *pcd[I_NPERC];           /* the instruments' numbers: perc_tab.h's (the tools may swap one) */
#ifdef LG_HOST
void pc_tune(int i, const pc_inst_t *d) { pcd[i] = d; }
#endif
static uint32_t pc_noise_s = 0x9E3779B9u;

static inline float pc_noise(void)              /* white, -1..1 */
{
    uint32_t x = pc_noise_s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    pc_noise_s = x;
    return (float)(int32_t)x * (1.0f / 2147483648.0f);
}

static float decay_a_sample(float t60) { return fm_exp2f(-9.965784f / (t60 * PC_SR)); }   /* 2^(-log2(1000)/n) */

/* ---- the recorded instruments: a stroke starts a voice that reads its recording forward at SAMP_RATE, linearly
 * interpolated to PC_SR, its level the stroke's and the recording's, a softer stroke darker (a one-pole lowpass at
 * 6 kHz + 14 kHz vel^2). At most SV_PER voices an instrument: the next one fades its oldest out in 5 ms; the hand
 * on it (闷, 止) fades them all in 15 ms */
#define SV_N 16
#define SV_PER 3
#define SV_GONE 1e-4f                           /* a fading voice this quiet is over */
typedef struct {
    const uint8_t *p, *end;                     /* its next sample, its end */
    float s0, s1, frac;                         /* the samples either side of where it reads, and how far between */
    float gain, g0, fmul, lp, lpk;              /* level, at the start; a fade's step (1: none); the lowpass */
    uint32_t age;                               /* (when it began: the oldest gives way) */
    int8_t inst;                                /* -1: free */
} sv_t;
static sv_t sv[SV_N];
static uint32_t sv_clock;
static uint8_t sv_turn[I_NPERC][SK_N];          /* which of two recordings next */
static float sv_fade_hand, sv_fade_steal, sv_step;

static int sampled(int i)
{
#ifdef PC_NO_SAMPLES                            /* (tools/perc_fit.py measures the modes) */
    (void)i;
    return 0;
#else
    return SAMP_SET[i].s[SK_OPEN][0] >= 0;
#endif
}

static void sv_fade(int i, float fmul)          /* every voice of instrument i fades out */
{
    int v;
    for (v = 0; v < SV_N; v++)
        if (sv[v].inst == i && sv[v].fmul > fmul)
            sv[v].fmul = fmul;
}

static void sv_start(int i, int kind, float vel)
{
    const samp_set_t *ss = &SAMP_SET[i];
    const samp_t *sm;
    int k = ss->s[kind][0] >= 0 ? kind : SK_OPEN, v = -1, j, live = 0, oldest = -1;
    float g = vel < 0.02f ? 0.02f : vel > 1.0f ? 1.0f : vel, fc;
    if (kind == SK_LIGHT && k == SK_OPEN)       /* (no light stroke recorded: the open one, softer) */
        g *= pcd[i]->light;
    sm = &SAMP[ss->s[k][ss->s[k][1] >= 0 ? (sv_turn[i][k] ^= 1u) : 0]];
    for (j = 0; j < SV_N; j++)                  /* the instrument's voices still sounding fully */
        if (sv[j].inst == i && sv[j].fmul >= 1.0f) {
            live++;
            if (oldest < 0 || sv[j].age < sv[oldest].age)
                oldest = j;
        }
    if (live >= SV_PER)
        sv[oldest].fmul = sv_fade_steal;
    for (j = 0; j < SV_N && v < 0; j++)         /* a free voice; else the quietest fading one; else the oldest */
        if (sv[j].inst < 0)
            v = j;
    for (j = 0; j < SV_N && v < 0; j++)
        if (sv[j].fmul < 1.0f)
            v = j;
    for (j = 0; j < SV_N; j++)
        if (v < 0 || (sv[v].fmul >= 1.0f && sv[j].age < sv[v].age))
            v = j;
    sv[v].p = SAMP_DATA + sm->off;
    sv[v].end = SAMP_DATA + sm->off + sm->len;
    sv[v].s0 = 0.0f;
    sv[v].s1 = (float)SAMP_MULAW[*sv[v].p++];
    sv[v].frac = 0.0f;
    sv[v].gain = sv[v].g0 = g * sm->level * (1.0f / 32768.0f);
    sv[v].fmul = 1.0f;
    fc = fm_minf(6000.0f + 14000.0f * g * g, 0.45f * PC_SR);
    sv[v].lpk = 1.0f - fm_expf(-2.0f * FM_PI * fc / PC_SR);
    sv[v].lp = 0.0f;
    sv[v].age = ++sv_clock;
    sv[v].inst = (int8_t)i;
}

static void sv_render(float *l, float *r, float *send, uint32_t n, const float *level)
{
    int v;
    uint32_t j;
    for (v = 0; v < SV_N; v++) {
        sv_t *q = &sv[v];
        int i = q->inst;
        float gl, gr, sd, s0, s1, fr, gain, lp, k;
        if (i < 0)
            continue;
        gl = pc[i].gl * level[i];
        gr = pc[i].gr * level[i];
        sd = pcd[i]->send * level[i];
        s0 = q->s0, s1 = q->s1, fr = q->frac, gain = q->gain, lp = q->lp, k = q->lpk;
        for (j = 0; j < n; j++) {
            float y;
            lp += k * ((s0 + (s1 - s0) * fr) - lp);
            y = lp * gain;
            l[j] += y * gl;
            r[j] += y * gr;
            send[j] += y * sd;
            fr += sv_step;
            while (fr >= 1.0f) {
                fr -= 1.0f;
                s0 = s1;
                s1 = q->p < q->end ? (float)SAMP_MULAW[*q->p++] : 0.0f;
            }
            gain *= q->fmul;
        }
        q->s0 = s0, q->s1 = s1, q->frac = fr, q->gain = gain, q->lp = lp;
        if ((q->p >= q->end && s0 == 0.0f && s1 == 0.0f) || gain < SV_GONE * q->g0)
            q->inst = -1;
    }
}


void pc_init(void)
{
    int i, k;
    sv_step = (float)SAMP_RATE / PC_SR;
    sv_fade_hand = fm_expf(-1.0f / (0.015f * PC_SR));
    sv_fade_steal = fm_expf(-1.0f / (0.005f * PC_SR));
    for (i = 0; i < I_NPERC; i++) {
        const pc_inst_t *d;
        pc_t *s = &pc[i];
        if (!pcd[i])
            pcd[i] = &PC_INST[i];
        d = pcd[i];
        float slow = 0.05f, pan = d->pan;
        for (k = 0; k < d->nmode && k < PC_MAXMODE; k++) {
            const pc_mode_t *m = &d->mode[k];
            s->w0[k] = 2.0f * FM_PI * m->hz / PC_SR;
            s->cw0[k] = fm_cosf(s->w0[k]);
            s->sw0[k] = fm_sinf(s->w0[k]);
            s->rfree[k] = decay_a_sample(m->t60 * d->ring);
            s->exn[k] = fm_sinf(s->w0[k]);
            s->rbloom[k] = 0.0f;
            if (m->bloom > 0.0f) {              /* e^-t/T1 - (1 - direct) e^-t/T2 peaks at amp: its peak found by steps */
                float t1 = m->t60 * d->ring / 6.9078f, t2 = m->bloom, mix = 1.0f - m->direct, peak = 0.0f, t;
                for (t = 0.0f; t < 4.0f * t2 + 0.05f; t += 0.002f)
                    peak = fm_maxf(peak, fm_expf(-t / t1) - mix * fm_expf(-t / t2));
                s->rbloom[k] = decay_a_sample(6.9078f * t2);
                s->bmix[k] = mix;
                s->exn[k] /= fm_maxf(peak, 0.05f);
            }
            if (m->t60 * d->ring > slow && m->amp > 0.05f)
                slow = m->t60 * d->ring;
        }
        for (k = 0; k < d->nband && k < PC_MAXBAND; k++) {
            {                                   /* a resonator at hz, bandwidth hz / q: at a stroke all-pole (a floor
                                                 * below it, 27 dB under its peak at 3 kHz: the hit's low thump), then
                                                 * within floor_t with two zeros at DC (the noise's second difference,
                                                 * chunk()), its peak kept (the gain over |1 - e^-jw|^2): the floor
                                                 * gone from the tail, which the recordings' don't have */
                float w = 2.0f * FM_PI * fm_minf(d->band[k].hz, 0.45f * PC_SR) / PC_SR;
                float r = fm_expf(-0.5f * w / d->band[k].q);
                s->bc1[k] = 2.0f * r * fm_cosf(w);
                s->bc2[k] = r * r;
                s->bga[k] = (1.0f - r * r) * 0.5f;
                s->bg[k] = s->bga[k] / (2.0f - 2.0f * fm_cosf(w));
            }
            s->bfree[k] = decay_a_sample(d->band[k].t60 * d->ring);
            if (d->band[k].t60 * d->ring > slow)
                slow = d->band[k].t60 * d->ring;
        }
        s->rmute = decay_a_sample(d->mute_t60);
        s->bmute = decay_a_sample(d->mute_t60 * 0.7f);
        s->amul = fm_expf(-(float)PC_CHUNK / (d->track_t * PC_SR));
        s->asmul = fm_expf(-(float)PC_CHUNK / (fm_maxf(d->slow_t, 0.01f) * PC_SR));
        s->cmul = decay_a_sample(d->click_t);
        s->ck = 1.0f - fm_expf(-2.0f * FM_PI * d->click_hz / PC_SR);
        s->chk = 1.0f - fm_expf(-2.0f * FM_PI * d->click_hp / PC_SR);
        s->lmul = fm_exp2f(-9.965784f * (float)PC_CHUNK / (slow * PC_SR));
        s->bxmul = d->floor_t > 0.0f ? fm_expf(-(float)PC_CHUNK / (d->floor_t * PC_SR)) : 1.0f;
        s->tk = 1.0f - fm_expf(-2.0f * FM_PI * fm_minf(d->tail_lp > 0.0f ? d->tail_lp : 20000.0f, 20000.0f) / PC_SR);
        s->lmute = fm_exp2f(-9.965784f * (float)PC_CHUNK / (d->mute_t60 * PC_SR));
        s->hk = 1.0f - fm_expf(-2.0f * FM_PI * fm_maxf(d->hp, 10.0f) / PC_SR);
        s->nk = 1.0f - fm_expf(-2.0f * FM_PI * fm_minf(d->nlp > 0.0f ? d->nlp : 20000.0f, 20000.0f) / PC_SR);
        s->gl = fm_sqrtf(0.5f * (1.0f - pan));
        s->gr = fm_sqrtf(0.5f * (1.0f + pan));
    }
    pc_panic();
}

void pc_panic(void)
{
    int i, k;
    for (k = 0; k < SV_N; k++)
        sv[k].inst = -1;
    for (i = 0; i < I_NPERC; i++) {
        pc_t *s = &pc[i];
        for (k = 0; k < PC_MAXMODE; k++)
            s->y1[k] = s->y2[k] = s->b1[k] = s->b2[k] = s->ex[k] = 0.0f;
        for (k = 0; k < PC_MAXBAND; k++) {
            s->benv[k] = 0.0f;
            s->bmul[k] = s->bfree[k];
            s->by1[k] = s->by2[k] = 0.0f;
        }
        s->a = s->as = s->click = s->clp = s->chp = s->life = s->h1 = s->h2 = s->n1 = s->n2 = 0.0f;
        s->nx1 = s->nx2 = s->bxf = s->t1 = s->t2 = 0.0f;
        s->hold = 1.0f;
        s->rolling = s->muted = s->awake = 0;
    }
}

/* a stroke: each mode's impulse (its level, the upper ones less when played softly), the bands, the contact */
static void strike(int i, float vel, float bright)
{
    const pc_inst_t *d = pcd[i];
    pc_t *s = &pc[i];
    int k, n = d->nmode;
    float lv = vel < 0.02f ? 0.02f : vel;
    for (k = 0; k < n && k < PC_MAXMODE; k++) {
        float up = n > 1 ? (float)k / (float)(n - 1) : 0.0f;
        float a = d->mode[k].amp * lv * fm_powf(lv, d->bright * up * bright);
        s->ex[k] += a * s->exn[k];
    }
    for (k = 0; k < d->nband && k < PC_MAXBAND; k++) {
        float up = d->nband > 1 ? (float)k / (float)(d->nband - 1) : 0.0f;
        s->benv[k] += d->band[k].amp * lv * fm_powf(lv, d->bright * up * bright);
    }
    s->click += d->click * lv;
    s->bxf = 1.0f;
    s->a = fm_minf(1.0f, s->a + lv * lv);       /* the glide grows with the energy (a gong's stiffening goes as the
                                                 * square of its swing), a stroke's on top of what still rings; no
                                                 * further than the hardest stroke's (a roll on the 大锣 holds its
                                                 * pitch up, it doesn't climb) */
    s->as = fm_minf(1.0f, s->as + lv * lv);
    s->life = fm_maxf(s->life, lv);
    s->awake = 1;
}

static void hand(int i, int on)                 /* the hand on the instrument (on), or off it */
{
    pc_t *s = &pc[i];
    int k;
    s->muted = (uint8_t)on;
    for (k = 0; k < PC_MAXBAND; k++)
        s->bmul[k] = on ? s->bmute : s->bfree[k];
    if (!on)
        s->hold = 1.0f;
}

static void sv_hit(int i, int stroke, float vel)   /* a recorded instrument's stroke (pc_hit) */
{
    pc_t *s = &pc[i];
    switch (stroke) {
    case PS_OPEN:
        sv_start(i, SK_OPEN, vel);
        break;
    case PS_LIGHT:
        sv_start(i, SK_LIGHT, vel);
        break;
    case PS_MUTE:                               /* struck and held: what rang stops, the stroke's own is short */
        sv_fade(i, sv_fade_hand);
        sv_start(i, SK_MUTE, vel);
        break;
    case PS_STOP:
        sv_fade(i, sv_fade_hand);
        break;
    case PS_DOUBLE:
        sv_start(i, SK_OPEN, vel);
        s->roll_left = 0.004f * PC_SR;          /* the second stick, 4 ms after */
        s->roll_vel = vel * 0.85f;
        s->roll_n = 0;
        s->rolling = 2;
        s->awake = 1;
        break;
    case PS_ROLL:
        s->rolling = 1;
        s->roll_vel = vel;
        s->roll_left = 0.0f;
        s->roll_n = 0;
        s->awake = 1;
        break;
    default:
        break;
    }
}

void pc_hit(int i, int stroke, float vel)
{
    pc_t *s;
    if (i < 0 || i >= I_NPERC)
        return;
    s = &pc[i];
    if (stroke != PS_ROLL)
        s->rolling = 0;
    if (stroke == PS_ROLLEND)
        return;
    if (sampled(i)) {
        sv_hit(i, stroke, vel);
        return;
    }
    switch (stroke) {
    case PS_OPEN:
        hand(i, 0);
        strike(i, vel, 1.0f);
        break;
    case PS_LIGHT:
        hand(i, 0);
        strike(i, vel * pcd[i]->light, 1.6f);
        break;
    case PS_MUTE:
        strike(i, vel, 1.0f);
        hand(i, 1);
        break;
    case PS_STOP:
        hand(i, 1);
        break;
    case PS_DOUBLE:                             /* two sticks a moment apart: one stroke louder, the bands fuller */
        hand(i, 0);
        strike(i, vel, 0.8f);
        s->roll_left = 0.004f * PC_SR;          /* the second stick, 4 ms after */
        s->roll_vel = vel * 0.85f;
        s->roll_n = 0;
        s->rolling = 2;
        break;
    case PS_ROLL:
        hand(i, 0);
        s->rolling = 1;
        s->roll_vel = vel;
        s->roll_left = 0.0f;
        s->roll_n = 0;
        s->awake = 1;
        break;
    default:
        break;
    }
}

float pc_ring(int i) { return i >= 0 && i < I_NPERC && pc[i].awake ? pc[i].life : 0.0f; }

static void roll_step(int i)                    /* a roll's next stroke (or the double's second stick), when due */
{
    pc_t *s = &pc[i];
    if (!s->rolling)
        return;
    s->roll_left -= (float)PC_CHUNK;
    if (s->roll_left > 0.0f)
        return;
    if (s->rolling == 2) {
        if (sampled(i))
            sv_start(i, SK_OPEN, s->roll_vel);
        else
            strike(i, s->roll_vel, 0.8f);
        s->rolling = 0;
    } else {                                    /* the sticks alternate, one a little weaker, never quite even */
        float v = s->roll_vel * ((s->roll_n & 1u) ? 0.82f : 1.0f) * (0.92f + 0.08f * pc_noise());
        if (sampled(i))
            sv_start(i, SK_OPEN, v);
        else
            strike(i, v, 1.0f);
        s->roll_left += PC_SR / pcd[i]->roll_hz * (1.0f + 0.06f * pc_noise());
        s->roll_n++;
    }
}

static void chunk(int i, float *mono)           /* PC_CHUNK frames of one instrument into mono (cleared here) */
{
    const pc_inst_t *d = pcd[i];
    pc_t *s = &pc[i];
    uint32_t j;
    int k;
    float a = s->a * (1.0f - d->slow) + s->as * d->slow, noise[PC_CHUNK], dn[PC_CHUNK];
    roll_step(i);
    if (s->muted)
        s->hold *= 0.80f;                       /* the hand presses: about 2 ms a chunk */
    for (j = 0; j < PC_CHUNK; j++) {
        mono[j] = 0.0f;
        noise[j] = pc_noise();
    }
#ifndef NO_MODES
    {                                           /* the modes: the living ones, two at a time */
        int live[PC_MAXMODE], nl = 0, q;
        float c1v[PC_MAXMODE], c2v[PC_MAXMODE], cwv[PC_MAXMODE], rv[PC_MAXMODE];
        for (k = 0; k < d->nmode && k < PC_MAXMODE; k++) {
            float r, dw, cw;
            if (s->ex[k] == 0.0f && fm_fabsf(s->y1[k]) + fm_fabsf(s->y2[k]) < PC_DEAD && s->rbloom[k] == 0.0f) {
                s->y1[k] = s->y2[k] = 0.0f;     /* died away: nothing to compute */
                continue;
            }
            r = s->rfree[k];
            if (s->rmute < r)
                r += (s->rmute - r) * (1.0f - s->hold);
            dw = s->w0[k] * d->mode[k].glide * a;   /* the glide, and cos(w0 + dw) to second order */
            cw = s->cw0[k] - dw * (s->sw0[k] + 0.5f * dw * s->cw0[k]);
            c1v[k] = 2.0f * r * cw;
            c2v[k] = r * r;
            cwv[k] = cw;
            rv[k] = r;
            live[nl++] = k;
        }
        for (q = 0; q + 1 < nl; q += 2) {       /* two resonators a pass: the frame's sum stored once */
            int ka = live[q], kb = live[q + 1];
            float a1 = c1v[ka], a2 = c2v[ka], b1 = c1v[kb], b2 = c2v[kb];
            float ya1 = s->y1[ka], ya2 = s->y2[ka], yb1 = s->y1[kb], yb2 = s->y2[kb], ya0, yb0;
            ya0 = a1 * ya1 - a2 * ya2 + s->ex[ka];
            yb0 = b1 * yb1 - b2 * yb2 + s->ex[kb];
            mono[0] += ya0 + yb0;
            ya2 = ya1, ya1 = ya0, yb2 = yb1, yb1 = yb0;
            for (j = 1; j < PC_CHUNK; j++) {
                ya0 = a1 * ya1 - a2 * ya2;
                yb0 = b1 * yb1 - b2 * yb2;
                mono[j] += ya0 + yb0;
                ya2 = ya1, ya1 = ya0, yb2 = yb1, yb1 = yb0;
            }
            s->y1[ka] = fm_flush(ya1), s->y2[ka] = fm_flush(ya2);
            s->y1[kb] = fm_flush(yb1), s->y2[kb] = fm_flush(yb2);
        }
        if (q < nl) {                           /* the odd one */
            int ka = live[q];
            float a1 = c1v[ka], a2 = c2v[ka], ya1 = s->y1[ka], ya2 = s->y2[ka], ya0;
            ya0 = a1 * ya1 - a2 * ya2 + s->ex[ka];
            mono[0] += ya0;
            ya2 = ya1, ya1 = ya0;
            for (j = 1; j < PC_CHUNK; j++) {
                ya0 = a1 * ya1 - a2 * ya2;
                mono[j] += ya0;
                ya2 = ya1, ya1 = ya0;
            }
            s->y1[ka] = fm_flush(ya1), s->y2[ka] = fm_flush(ya2);
        }
        for (q = 0; q < nl; q++) {              /* a swelling mode: less its rise, which dies sooner */
            int kk = live[q];
            if (s->rbloom[kk] > 0.0f) {
                float rb = fm_minf(s->rbloom[kk], rv[kk]), b1 = s->b1[kk], b2 = s->b2[kk], b0;
                float d1 = 2.0f * rb * cwv[kk], d2 = rb * rb, g = -s->bmix[kk];
                b0 = d1 * b1 - d2 * b2 + s->ex[kk];
                mono[0] += g * b0;
                b2 = b1, b1 = b0;
                for (j = 1; j < PC_CHUNK; j++) {
                    b0 = d1 * b1 - d2 * b2;
                    mono[j] += g * b0;
                    b2 = b1, b1 = b0;
                }
                s->b1[kk] = fm_flush(b1), s->b2[kk] = fm_flush(b2);
            }
            s->ex[kk] = 0.0f;
        }
    }
#endif
#ifndef NO_BANDS
    if (d->nband) {                             /* the bands' noise, darker; and its second difference (dn) */
        float n1 = s->n1, n2 = s->n2, kk = s->nk, x1 = s->nx1, x2 = s->nx2, t1 = s->t1, t2 = s->t2, tk = s->tk;
        for (j = 0; j < PC_CHUNK; j++) {
            n1 += kk * (noise[j] - n1);
            n2 += kk * (n1 - n2);
            noise[j] = n2;
            t1 += tk * (n2 - 2.0f * x1 + x2 - t1);
            t2 += tk * (t1 - t2);
            dn[j] = t2;
            x2 = x1, x1 = n2;
        }
        s->n1 = n1;
        s->n2 = n2;
        s->nx1 = x1;
        s->nx2 = x2;
        s->t1 = fm_flush(t1);
        s->t2 = fm_flush(t2);
    }
    for (k = 0; k < d->nband && k < PC_MAXBAND; k++) {   /* the bands: noise through a resonator, dying away */
        float e = s->benv[k], m = s->bmul[k], c1 = s->bc1[k], c2 = s->bc2[k], y1 = s->by1[k], y2 = s->by2[k], y0;
        float gb = s->bg[k] * (1.0f - s->bxf), ga = s->bga[k] * s->bxf;
        if (e < 3e-5f) {
            s->benv[k] = 0.0f;
            continue;
        }
        for (j = 0; j < PC_CHUNK; j++) {
            y0 = c1 * y1 - c2 * y2 + (gb * dn[j] + ga * noise[j]) * e;
            mono[j] += y0;
            y2 = y1, y1 = y0;
            e *= m;
        }
        s->benv[k] = e;
        s->by1[k] = fm_flush(y1), s->by2[k] = fm_flush(y2);
    }
    s->bxf *= s->bxmul;
#endif
    if (s->click > 1e-5f) {                     /* the contact: noise, band-limited, dying away */
        float c = s->click, lp = s->clp, hp = s->chp;
        for (j = 0; j < PC_CHUNK; j++) {
            lp += s->ck * (pc_noise() - lp);
            hp += s->chk * (lp - hp);
            mono[j] += (lp - hp) * c;
            c *= s->cmul;
        }
        s->click = c;
        s->clp = lp;
        s->chp = hp;
    }
    {                                           /* the highpass: less its lowpass, twice */
        float h1 = s->h1, h2 = s->h2, k1 = s->hk;
        for (j = 0; j < PC_CHUNK; j++) {
            h1 += k1 * (mono[j] - h1);
            mono[j] -= h1;
            h2 += k1 * (mono[j] - h2);
            mono[j] -= h2;
        }
        s->h1 = fm_flush(h1);
        s->h2 = fm_flush(h2);
    }
    s->a *= s->amul;
    s->as *= s->asmul;
    s->life *= s->muted ? s->lmute : s->lmul;
    if (!s->rolling && s->life < PC_QUIET && s->click < 1e-5f) {
        s->awake = 0;
        for (k = 0; k < PC_MAXMODE; k++)
            s->y1[k] = s->y2[k] = s->b1[k] = s->b2[k] = 0.0f;
        for (k = 0; k < PC_MAXBAND; k++)
            s->benv[k] = 0.0f;
    }
}

void pc_render(float *l, float *r, float *send, uint32_t n, const float *level)
{
    uint32_t j, at;
    int i;
    float mono[PC_CHUNK];
    for (at = 0; at + PC_CHUNK <= n; at += PC_CHUNK) {
        for (i = 0; i < I_NPERC; i++) {
            float gl, gr, sd;
            if (!pc[i].awake)
                continue;
            if (sampled(i)) {                   /* (its voices below; here only a roll's strokes) */
                roll_step(i);
                pc[i].awake = pc[i].rolling != 0;
                continue;
            }
            chunk(i, mono);
            gl = pc[i].gl * level[i];
            gr = pc[i].gr * level[i];
            sd = pcd[i]->send * level[i];
            for (j = 0; j < PC_CHUNK; j++) {
                l[at + j] += mono[j] * gl;
                r[at + j] += mono[j] * gr;
                send[at + j] += mono[j] * sd;
            }
        }
        sv_render(l + at, r + at, send + at, PC_CHUNK, level);
    }
}
