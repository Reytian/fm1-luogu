/* SPDX-License-Identifier: MPL-2.0 */
/* The bowed instruments' bodies and the voices' numbers (hu.h). HU_BODY: tools/hu_fit.py fit, from the 京胡 tracks of
 * the Jingju a Cappella Recordings Collection (CC BY 4.0) and the CTIS 二胡; the rest set by ear. */
#pragma once

typedef struct {                                /* a body: biquads in a row (RBJ), from tools/hu_fit.py fit */
    float hp_hz, hp_q, lp_hz, lp_q;
    float peak[6][3];                           /* {centre (Hz), gain (dB), Q} */
} hu_body_t;
static const hu_body_t HU_BODY[HU_NKIND] = {   /* tools/hu_fit.py fit: hp, lp, peaks {Hz, dB, Q} */
    {600.0f, 0.900f, 18054.1f, 0.900f, {{641.3f, 15.00f, 3.995f}, {4089.6f, 14.66f, 1.018f}, {1570.6f, 14.04f, 0.500f}, {2279.7f, 15.00f, 2.000f}, {9483.6f, 8.14f, 0.500f}, {1862.4f, 7.73f, 6.000f}}},   /* jinghu */
    {600.0f, 0.900f, 6271.5f, 0.500f, {{467.2f, 15.00f, 2.397f}, {1557.5f, 15.00f, 2.967f}, {757.1f, 10.73f, 6.000f}, {2408.0f, -9.32f, 5.091f}, {3656.2f, -2.92f, 6.000f}, {1793.7f, 15.00f, 4.020f}}},   /* erhu */
};

typedef struct {
    float gain;                                 /* the level: in a 过门 (K-weighted, momentary) the 京二胡 sits 5 dB
                                                 * under the 京胡, the 月琴 (yq_par_t) 7 dB, and the three together
                                                 * about where the 锣鼓 that lead into it do (lg.c) */
    float attack_s;                             /* a new bow's rise */
    float scratch, scratch_s;                   /* a new bow's scratch: its level and its decay */
    float noise, noise_hz, noise_q;             /* the bow's noise: its level re. the bow, its band */
    float tilt;                                 /* dB an octave darker when the bow is lightest */
    float vib_hz;                               /* the vibrato's rate */
    float finger_s;                             /* a change of finger: how long the pitch takes */
    float pan, send;
} hu_par_t;


static const hu_par_t HU_PAR[HU_NKIND] = {
    {0.00402f, 0.025f, 0.30f, 0.030f, 0.020f, 2500.0f, 0.8f, 3.0f, 5.8f, 0.012f, 0.15f, 0.25f},   /* 京胡 */
    {0.0178f, 0.045f, 0.20f, 0.040f, 0.015f, 1500.0f, 0.8f, 4.0f, 5.5f, 0.015f, 0.35f, 0.25f},   /* 京二胡 */
};

typedef struct {
    float decay_s, decay_pow;                   /* a string's ring to -60 dB at 220 Hz, and how it shortens higher up
                                                 * (t60 = decay_s (220 / f) ^ decay_pow: the CTIS 月琴's scale rings
                                                 * to -40 dB in 4.8 s at A3 and in 0.25 s at G5) */
    float pick_lp;                              /* the pick's softness (a one-pole's coefficient) */
    float click;                                /* the plectrum's own click */
    float body_hz[2], body_q[2], body_g[2];     /* the round body's two resonances */
    float direct, gain, send;
} yq_par_t;
static const yq_par_t YQ_PAR = {7.2f, 2.3f, 0.5f, 0.05f, {420.0f, 1250.0f}, {2.0f, 2.5f}, {0.8f, 0.6f}, 0.6f, 0.251f, 0.25f};
