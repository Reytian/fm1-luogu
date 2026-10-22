/* SPDX-License-Identifier: MPL-2.0 */
/* A state-variable filter, topology-preserving (Zavalishin): lowpass, bandpass and highpass of one input. */
#pragma once
#include "fastmath.h"

typedef struct {
    float g, k, a1, a2, a3, ic1, ic2;
} pc_svf2_t;

static inline void svf2_set(pc_svf2_t *f, float hz, float q)
{
    float g = fm_tanf(FM_PI * fm_minf(hz, 0.45f * 44100.0f) / 44100.0f);
    f->g = g;
    f->k = 1.0f / q;
    f->a1 = 1.0f / (1.0f + g * (g + f->k));
    f->a2 = g * f->a1;
    f->a3 = g * f->a2;
}

static inline float svf2(pc_svf2_t *f, float x, float *bp, float *hp)   /* returns the lowpass */
{
    float v3 = x - f->ic2, v1 = f->a1 * f->ic1 + f->a2 * v3, v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
    f->ic1 = 2.0f * v1 - f->ic1;
    f->ic2 = 2.0f * v2 - f->ic2;
    *bp = v1;
    *hp = x - f->k * v1 - v2;
    return v2;
}
