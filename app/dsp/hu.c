/* SPDX-License-Identifier: MPL-2.0 */
/* LUOGU's 文场 (hu.h).
 *   bowed  a string under a bow, as a source and a body: a sawtooth (the Helmholtz motion; band-limited by polyBLEP),
 *          darker when the bow is light (a one-pole lowpass), the bow's noise beside it, through the instrument's body
 *          (hu_tab.h HU_BODY: a highpass, six peaks and a lowpass, fitted by tools/hu_fit.py to the spectral envelope
 *          of real 京胡 and 二胡 notes), so vibrato and slides move the partials through the body's resonances as
 *          on the real instrument. Pitch, vibrato and the bow's level are updated every HU_SUB frames.
 *   月琴    plucked strings: Karplus-Strong, a pick of filtered noise, three voices.
 * One compilation unit, -O2. No doubles, no libm (fastmath.h). */
#include "hu.h"
#include "fastmath.h"
#include "svf.h"
#include "hu_tab.h"                             /* HU_BODY[HU_NKIND], HU_PAR[HU_NKIND], YQ_PAR */

#if defined(LG_HOST) || defined(PLAT_HOST)
#define HU_POOL
#else
#define HU_POOL __attribute__((section(".pool")))
#endif

#define HU_SUB 32u                              /* frames between control updates */
#define NBQ 8                                   /* the body's biquads */

static float hu_sin[1025] HU_POOL;
static inline float sin_u(uint32_t ph)          /* sin(2 pi ph / 2^32), interpolated */
{
    uint32_t i = ph >> 22;
    float f = (float)(ph & 0x3FFFFFu) * (1.0f / 4194304.0f);
    return hu_sin[i] + f * (hu_sin[i + 1] - hu_sin[i]);
}
static uint32_t hu_noise_s = 0x2545F491u;
static inline float hu_noise(void)
{
    uint32_t x = hu_noise_s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    hu_noise_s = x;
    return (float)(int32_t)x * (1.0f / 2147483648.0f);
}
static inline float mtof(float m) { return 440.0f * fm_exp2f((m - 69.0f) * (1.0f / 12.0f)); }
static inline float k_of(float seconds, float rate) { return 1.0f - fm_expf(-1.0f / (seconds * rate)); }

/* ------------------------------------------------------------------ the body's biquads (RBJ, transposed form II) */
typedef struct {
    float b0, b1, b2, a1, a2;
} bq_c;
static bq_c body[HU_NKIND][NBQ];

static void bq_design(bq_c *c, int kind, float fc, float gain_db, float q)   /* kind 0 peak, 1 highpass, 2 lowpass */
{
    float w0 = 2.0f * FM_PI * fm_minf(fc, 0.45f * HU_SR) / HU_SR, cw = fm_cosf(w0), sw = fm_sinf(w0);
    float alpha = sw / (2.0f * q), A = fm_exp2f(gain_db * (1.0f / 40.0f) * 3.3219281f), b0, b1, b2, a0, a1, a2;
    if (kind == 0) {
        b0 = 1.0f + alpha * A;
        b1 = -2.0f * cw;
        b2 = 1.0f - alpha * A;
        a0 = 1.0f + alpha / A;
        a1 = -2.0f * cw;
        a2 = 1.0f - alpha / A;
    } else if (kind == 1) {
        b0 = (1.0f + cw) * 0.5f;
        b1 = -(1.0f + cw);
        b2 = b0;
        a0 = 1.0f + alpha;
        a1 = -2.0f * cw;
        a2 = 1.0f - alpha;
    } else {
        b0 = (1.0f - cw) * 0.5f;
        b1 = 1.0f - cw;
        b2 = b0;
        a0 = 1.0f + alpha;
        a1 = -2.0f * cw;
        a2 = 1.0f - alpha;
    }
    c->b0 = b0 / a0;
    c->b1 = b1 / a0;
    c->b2 = b2 / a0;
    c->a1 = a1 / a0;
    c->a2 = a2 / a0;
}

/* ------------------------------------------------------------------ bowed */
typedef struct {
    int kind;
    float pitch;                                /* MIDI, without the vibrato */
    float p_from, p_to, p_t, p_len;             /* a slide under way: from, to, time so far, length (s) */
    float vib, vib_delay, vib_age, vib_amt;     /* vibrato: depth (cents), its delay, the note's age, its fade in */
    uint32_t vib_ph, vib_inc;
    float amp, amp_to, amp_k, damp;             /* the bow: its level, where it goes, how fast; the level's step a
                                                 * frame (a ramp over the sub-block) */
    float ph, inc;                              /* the saw's phase and step (cycles) */
    float lp, lpk;                              /* the bow's weight: a one-pole lowpass on the saw */
    float scratch, smul;                        /* a new bow's scratch */
    uint8_t on;
} bow_t;
static bow_t bw[HU_NVOICE];
/* the bodies: one a kind, the voices of a kind through it together (it is linear: the band's 京胡 and the player's
 * share one), ringing on a little after the last bow lifts */
static float bz1[HU_NKIND][NBQ], bz2[HU_NKIND][NBQ];
static uint16_t body_tail[HU_NKIND];

static void bow_reset(bow_t *b)
{
    b->amp = b->amp_to = b->damp = 0.0f;
    b->scratch = 0.0f;
    b->lp = 0.0f;
    b->on = 0;
}

void hu_kind(int v, int kind)
{
    if (v >= 0 && v < HU_NVOICE && kind >= 0 && kind < HU_NKIND && bw[v].kind != kind) {
        bow_reset(&bw[v]);
        bw[v].kind = kind;
    }
}

void hu_on(int v, const hu_note_t *n)
{
    bow_t *b;
    const hu_par_t *p;
    float f;
    if (v < 0 || v >= HU_NVOICE)
        return;
    b = &bw[v];
    p = &HU_PAR[b->kind];
    if (n->legato && b->on && b->amp_to > 0.0f) {   /* a change of finger: the pitch moves, the bow goes on */
        b->p_from = b->pitch;
        b->p_to = n->midi;
        b->p_t = 0.0f;
        b->p_len = n->slide_from != 0.0f ? n->slide_s : p->finger_s;
    } else {                                    /* a new bow */
        b->pitch = n->midi + n->slide_from;
        b->p_from = b->pitch;
        b->p_to = n->midi;
        b->p_t = 0.0f;
        b->p_len = n->slide_from != 0.0f ? n->slide_s : 0.0f;
        b->amp_k = k_of(p->attack_s, HU_SR / (float)HU_SUB);
        b->scratch = p->scratch * (0.4f + 0.6f * n->vel);
        b->smul = fm_expf(-1.0f / (p->scratch_s * HU_SR));
        b->vib_age = 0.0f;
        b->vib_amt = 0.0f;
        if (!b->on)
            b->ph = 0.0f;
    }
    b->vib = n->vib;
    b->vib_delay = n->vib_delay;
    b->vib_inc = (uint32_t)(p->vib_hz * (4294967296.0f / HU_SR) * (float)HU_SUB);
    b->amp_to = 0.25f + 0.75f * n->vel;
    f = 1500.0f + 9000.0f * n->vel * n->vel;    /* the bow's weight: a light bow, a darker tone */
    b->lpk = 1.0f - fm_expf(-2.0f * FM_PI * f / HU_SR);
    b->on = 1;
}

void hu_off(int v, float release_s)
{
    if (v < 0 || v >= HU_NVOICE || !bw[v].on)
        return;
    bw[v].amp_to = 0.0f;
    bw[v].amp_k = k_of(release_s > 0.005f ? release_s : 0.005f, HU_SR / (float)HU_SUB);
}

void hu_glide(int v, float midi, float seconds)
{
    bow_t *b;
    if (v < 0 || v >= HU_NVOICE || !bw[v].on)
        return;
    b = &bw[v];
    b->p_from = b->pitch;
    b->p_to = midi;
    b->p_t = 0.0f;
    b->p_len = seconds;
}

float hu_pitch(int v) { return v >= 0 && v < HU_NVOICE && bw[v].on && bw[v].amp_to > 0.0f ? bw[v].pitch : -1.0f; }

static void bow_control(bow_t *b)               /* the next HU_SUB frames: pitch (slide, vibrato), the bow's level */
{
    const hu_par_t *p = &HU_PAR[b->kind];
    const float dt = (float)HU_SUB / HU_SR;
    float pitch, target;
    if (b->p_len > 0.0f && b->p_t < b->p_len) { /* a slide: eased at both ends, as a finger moves */
        float x;
        b->p_t += dt;
        x = fm_minf(1.0f, b->p_t / b->p_len);
        x = x * x * (3.0f - 2.0f * x);
        b->pitch = b->p_from + (b->p_to - b->p_from) * x;
    } else {
        b->pitch = b->p_to;
    }
    b->vib_age += dt;
    if (b->vib > 0.0f && b->vib_age > b->vib_delay)
        b->vib_amt += (1.0f - b->vib_amt) * (dt / 0.25f);   /* the vibrato comes in over a quarter second */
    else
        b->vib_amt *= 0.9f;
    b->vib_ph += b->vib_inc;
    pitch = b->pitch + b->vib * b->vib_amt * 0.01f * sin_u(b->vib_ph);
    b->inc = mtof(pitch) * (1.0f / HU_SR);
    if (b->inc > 0.45f)
        b->inc = 0.45f;
    target = b->amp + (b->amp_to - b->amp) * b->amp_k;
    b->damp = (target - b->amp) * (1.0f / (float)HU_SUB);
    if (b->amp_to <= 0.0f && target < 1e-4f && b->scratch < 1e-4f)
        bow_reset(b);
    (void)p;
}

static inline float blep(float t, float dt)     /* polyBLEP: the saw's step, band-limited */
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

static void bow_src(bow_t *b, float *out, uint32_t n, float level)   /* the source (before the body), times the
                                                                     * voice's level, added into out */
{
    const hu_par_t *p = &HU_PAR[b->kind];
    float ph, inc, lp, lpk, amp, damp, sc, smul, nz = p->noise, g = p->gain * level;
    uint32_t j;
    bow_control(b);
    if (!b->on)
        return;
    ph = b->ph, inc = b->inc, lp = b->lp, lpk = b->lpk, amp = b->amp, damp = b->damp, sc = b->scratch, smul = b->smul;
    for (j = 0; j < n; j++) {
        float saw;
        ph += inc;
        if (ph >= 1.0f)
            ph -= 1.0f;
        saw = 2.0f * ph - 1.0f - blep(ph, inc);
        lp += lpk * (saw - lp);
        out[j] += ((lp + nz * hu_noise()) * amp + sc * hu_noise()) * g;
        amp += damp;
        sc *= smul;
    }
    b->ph = ph, b->lp = lp, b->amp = amp, b->scratch = sc;
}

static void body_run(int kind, float *x, uint32_t n)   /* the body, in place */
{
    const bq_c *c = body[kind];
    float *z1 = bz1[kind], *z2 = bz2[kind];
    uint32_t j;
    int k;
    for (j = 0; j < n; j++) {
        float v = x[j];
        for (k = 0; k < NBQ; k++) {
            float y = c[k].b0 * v + z1[k];
            z1[k] = c[k].b1 * v - c[k].a1 * y + z2[k];
            z2[k] = c[k].b2 * v - c[k].a2 * y;
            v = y;
        }
        x[j] = v;
    }
    for (k = 0; k < NBQ; k++) {
        z1[k] = fm_flush(z1[k]);
        z2[k] = fm_flush(z2[k]);
    }
}

/* ------------------------------------------------------------------ 月琴 */
#define YQ_N 3
#define YQ_MAXD 512                             /* the longest string: 86 Hz */
typedef struct {
    float d[YQ_MAXD];
    float len, lp, g, ap_c, ap_x1, ap_y1;
    int w;
    float pick;                                 /* the pick's own click, fading */
    uint32_t left;                              /* frames until it is quiet enough to stop */
    uint32_t age;
} yq_t;
static yq_t yq[YQ_N] HU_POOL;
static uint32_t yq_clock;
static pc_svf2_t yq_body[2];

void yq_pluck(float midi, float vel)
{
    int i, pick = 0;
    yq_t *s;
    float f = mtof(midi), len, frac, t60;
    uint32_t best = 0xFFFFFFFFu;
    for (i = 0; i < YQ_N; i++)                  /* the oldest string */
        if (yq[i].age < best)
            best = yq[i].age, pick = i;
    s = &yq[pick];
    len = HU_SR / f - 0.5f;                     /* (the loop's lowpass delays half a sample) */
    if (len > (float)(YQ_MAXD - 2))
        len = (float)(YQ_MAXD - 2);
    s->len = (float)(int)len;
    frac = len - s->len;
    s->ap_c = (1.0f - frac) / (1.0f + frac);    /* the fraction by a first-order allpass */
    s->ap_x1 = s->ap_y1 = 0.0f;
    t60 = YQ_PAR.decay_s * fm_powf(220.0f / f, YQ_PAR.decay_pow);
    t60 = t60 < 0.15f ? 0.15f : t60 > 8.0f ? 8.0f : t60;
    s->g = fm_exp2f(-9.965784f / (t60 * f));   /* -60 dB in t60 */
    s->lp = 0.0f;
    for (i = 0; i < (int)s->len; i++) {         /* the pick: noise, softened by the plectrum's width */
        s->lp += YQ_PAR.pick_lp * (hu_noise() - s->lp);
        s->d[i] = s->lp * vel;
    }
    s->lp = 0.0f;
    s->w = 0;
    s->pick = YQ_PAR.click * vel;
    s->left = (uint32_t)(1.2f * t60 * HU_SR);
    s->age = ++yq_clock;
}

void yq_damp(void)                              /* the fingers on the strings: about 50 ms */
{
    int i;
    for (i = 0; i < YQ_N; i++)
        if (yq[i].left) {
            yq[i].g *= 0.97f;
            if (yq[i].left > 4000u)
                yq[i].left = 4000u;
        }
}

static float yq_tick(void)
{
    float y = 0.0f;
    int i;
    for (i = 0; i < YQ_N; i++) {
        yq_t *s = &yq[i];
        float x, ap;
        if (!s->left)
            continue;
        s->left--;
        x = s->d[s->w];
        s->lp = 0.5f * (x + s->lp);             /* the loop's loss: a two-point average */
        ap = s->ap_c * (s->lp - s->ap_y1) + s->ap_x1;
        s->ap_x1 = s->lp;
        s->ap_y1 = ap;
        s->d[s->w] = ap * s->g;
        if (++s->w >= (int)s->len)
            s->w = 0;
        y += x + s->pick * hu_noise();
        s->pick *= 0.995f;
    }
    return y;
}

/* ------------------------------------------------------------------ */
void hu_init(void)
{
    int i;
    for (i = 0; i <= 1024; i++)
        hu_sin[i] = fm_sinf(2.0f * FM_PI * (float)i / 1024.0f);
    for (i = 0; i < HU_NKIND; i++) {             /* the bodies: highpass, lowpass, the six peaks */
        const hu_body_t *d = &HU_BODY[i];
        int k;
        bq_design(&body[i][0], 1, d->hp_hz, 0.0f, d->hp_q);
        bq_design(&body[i][1], 2, d->lp_hz, 0.0f, d->lp_q);
        for (k = 0; k < 6; k++)
            bq_design(&body[i][2 + k], 0, d->peak[k][0], d->peak[k][1], d->peak[k][2]);
    }
    for (i = 0; i < HU_NVOICE; i++) {
        bw[i].kind = -1;
        hu_kind(i, i == HV_BAND_ER ? HU_ERHU : HU_JINGHU);
    }
    svf2_set(&yq_body[0], YQ_PAR.body_hz[0], YQ_PAR.body_q[0]);
    svf2_set(&yq_body[1], YQ_PAR.body_hz[1], YQ_PAR.body_q[1]);
    hu_panic();
}

void hu_panic(void)
{
    int i;
    for (i = 0; i < HU_NVOICE; i++)
        bow_reset(&bw[i]);
    for (i = 0; i < HU_NKIND; i++) {
        int k;
        for (k = 0; k < NBQ; k++)
            bz1[i][k] = bz2[i][k] = 0.0f;
        body_tail[i] = 0;
    }
    for (i = 0; i < YQ_N; i++)
        yq[i].left = 0;
}

void hu_render(float *l, float *r, float *send, uint32_t n, const float *level, float lv_yq)
{
    uint32_t at, j;
    int v, kd;
    float src[HU_NKIND][HU_SUB];
    for (at = 0; at < n; at += HU_SUB) {
        uint32_t m = n - at < HU_SUB ? n - at : HU_SUB;
        for (kd = 0; kd < HU_NKIND; kd++)
            for (j = 0; j < m; j++)
                src[kd][j] = 0.0f;
        for (v = 0; v < HU_NVOICE; v++)
            if (bw[v].on) {
                bow_src(&bw[v], src[bw[v].kind], m, level[v]);
                body_tail[bw[v].kind] = 64;     /* (the body rings on about 45 ms after) */
            }
        for (kd = 0; kd < HU_NKIND; kd++) {
            const hu_par_t *p = &HU_PAR[kd];
            float gl, gr, sd;
            if (!body_tail[kd])
                continue;
            body_tail[kd]--;
            body_run(kd, src[kd], m);
            gl = fm_sqrtf(0.5f * (1.0f - p->pan));
            gr = fm_sqrtf(0.5f * (1.0f + p->pan));
            sd = p->send;
            for (j = 0; j < m; j++) {
                l[at + j] += src[kd][j] * gl;
                r[at + j] += src[kd][j] * gr;
                send[at + j] += src[kd][j] * sd;
            }
        }
        if (lv_yq > 0.0f && (yq[0].left || yq[1].left || yq[2].left))
            for (j = 0; j < m; j++) {
                float y = yq_tick(), bp0, bp1, hp, o;
                svf2(&yq_body[0], y, &bp0, &hp);
                svf2(&yq_body[1], y, &bp1, &hp);
                o = (YQ_PAR.direct * y + YQ_PAR.body_g[0] * bp0 + YQ_PAR.body_g[1] * bp1) * lv_yq * YQ_PAR.gain;
                l[at + j] += o * 0.8f;
                r[at + j] += o * 0.6f;
                send[at + j] += o * YQ_PAR.send;
            }
    }
}
