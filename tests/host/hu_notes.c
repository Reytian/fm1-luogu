/* SPDX-License-Identifier: MPL-2.0 */
/* The bowed voices and the 月琴, note by note, each in a WAV (for tools/hu_fit.py): hu_notes OUTDIR */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "hu.h"
#include "wav.h"

static void run(const char *path, int voice, int kind, float midi, float secs, int pluck)
{
    float level[HU_NVOICE] = {1.0f, 1.0f, 1.0f};
    wav_t w;
    uint32_t n = (uint32_t)((secs + 0.5f) * 44100.0f), at;
    hu_note_t nt = {midi, 0.8f, 0.0f, 0.0f, 0.0f, 0.3f, 0};
    hu_panic();
    hu_kind(voice, kind);
    if (pluck)
        yq_pluck(midi, 0.9f);
    else
        hu_on(voice, &nt);
    wav_open(&w, path, 1);
    for (at = 0; at < n; at += 256u) {
        float l[256], r[256], s[256], m[256];
        uint32_t j;
        memset(l, 0, sizeof l), memset(r, 0, sizeof r), memset(s, 0, sizeof s);
        if (!pluck && at >= (uint32_t)(secs * 44100.0f) && at < (uint32_t)(secs * 44100.0f) + 256u)
            hu_off(voice, 0.08f);
        hu_render(l, r, s, 256u, level, 1.0f);
        for (j = 0; j < 256u; j++)
            m[j] = (l[j] + r[j]) * 0.7071f;
        wav_put(&w, m, 256u);
    }
    wav_close(&w);
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : ".";
    static const float PITCH[] = {64, 69, 74, 79, 84};
    char path[512];
    unsigned i;
    hu_init();
    for (i = 0; i < sizeof PITCH / sizeof PITCH[0]; i++) {
        snprintf(path, sizeof path, "%s/jinghu_%d.wav", out, (int)PITCH[i]);
        run(path, HV_BAND_HU, HU_JINGHU, PITCH[i], 1.5f, 0);
        snprintf(path, sizeof path, "%s/erhu_%d.wav", out, (int)PITCH[i] - 12);
        run(path, HV_BAND_ER, HU_ERHU, PITCH[i] - 12.0f, 1.5f, 0);
        snprintf(path, sizeof path, "%s/yueqin_%d.wav", out, (int)PITCH[i] - 7);
        run(path, 0, 0, PITCH[i] - 7.0f, 1.5f, 1);
    }
    return 0;
}
