/* SPDX-License-Identifier: MPL-2.0 */
/* One open stroke of one instrument with some of its numbers replaced, as raw float32 on stdout (tools/perc_fit.py
 * tune searches the contact and the brightness this way):
 *   perc_tune INST SECONDS VEL CLICK CLICK_T CLICK_LP CLICK_HP BRIGHT */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "perc.h"
#include "perc_tab.h"

int main(int argc, char **argv)
{
    pc_inst_t d;
    int i;
    float secs, level[I_NPERC];
    uint32_t n, at;
    if (argc < 9) {
        fprintf(stderr, "usage: perc_tune INST SECONDS VEL CLICK CLICK_T CLICK_LP CLICK_HP BRIGHT\n");
        return 2;
    }
    i = atoi(argv[1]);
    d = PC_INST[i];
    secs = (float)atof(argv[2]);
    d.click = (float)atof(argv[4]);
    d.click_t = (float)atof(argv[5]);
    d.click_hz = (float)atof(argv[6]);
    d.click_hp = (float)atof(argv[7]);
    d.bright = (float)atof(argv[8]);
    pc_tune(i, &d);
    pc_init();
    for (at = 0; at < I_NPERC; at++)
        level[at] = 1.0f;
    pc_hit(i, PS_OPEN, (float)atof(argv[3]));
    n = (uint32_t)(secs * 44100.0f);
    for (at = 0; at < n; at += 256u) {
        float l[256], r[256], s[256], m[256];
        uint32_t j;
        memset(l, 0, sizeof l), memset(r, 0, sizeof r), memset(s, 0, sizeof s);
        pc_render(l, r, s, 256u, level);
        for (j = 0; j < 256u; j++)
            m[j] = (l[j] + r[j]) * 0.7071f;
        fwrite(m, sizeof(float), 256u, stdout);
    }
    return 0;
}
