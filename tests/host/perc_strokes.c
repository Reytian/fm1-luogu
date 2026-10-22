/* SPDX-License-Identifier: MPL-2.0 */
/* Every instrument's strokes, one after another, each in a WAV of its own (for tools/perc_fit.py to compare with the
 * recordings): perc_strokes OUTDIR [SECONDS] */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "perc.h"
#include "wav.h"

static const char *const INAME[I_NPERC] = {"gu", "ban", "daluo", "naobo", "xiaoluo", "tanggu"};
static const char *const SNAME[PS_NSTROKE] = {"open", "light", "mute", "stop", "double", "roll"};

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : ".";
    float secs = argc > 2 ? (float)atof(argv[2]) : 3.0f;
    float level[I_NPERC];
    int i, s;
    pc_init();
    for (i = 0; i < I_NPERC; i++)
        level[i] = 1.0f;
    for (i = 0; i < I_NPERC; i++)
        for (s = 0; s < PS_NSTROKE; s++) {
            char path[512];
            wav_t w;
            uint32_t n = (uint32_t)(secs * 44100.0f), at;
            float peak = 0.0f;
            if (s == PS_STOP)
                continue;
            snprintf(path, sizeof path, "%s/%s_%s.wav", out, INAME[i], SNAME[s]);
            if (wav_open(&w, path, 1)) {
                perror(path);
                return 1;
            }
            pc_panic();
            pc_hit(i, s, 1.0f);
            for (at = 0; at < n; at += 256u) {
                float l[256], r[256], sd[256], m[256];
                uint32_t j;
                memset(l, 0, sizeof l), memset(r, 0, sizeof r), memset(sd, 0, sizeof sd);
                if (s == PS_ROLL && at >= 44100u && at < 44100u + 256u)
                    pc_hit(i, PS_STOP, 0.0f);   /* a second's roll, then the hand */
                pc_render(l, r, sd, 256u, level);
                for (j = 0; j < 256u; j++) {
                    m[j] = 0.1f * (l[j] + r[j]) * 0.7071f;   /* (well under full scale: the 16-bit file must not clip) */
                    if (m[j] > peak || -m[j] > peak)
                        peak = m[j] > 0 ? m[j] : -m[j];
                }
                wav_put(&w, m, 256u);
            }
            wav_close(&w);
            printf("%-8s %-7s peak %.3f\n", INAME[i], SNAME[s], (double)peak);
        }
    return 0;
}
