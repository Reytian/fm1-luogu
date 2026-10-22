/* SPDX-License-Identifier: MPL-2.0 */
/* LUOGU's percussion, the 武场: 板鼓, 檀板, 大锣, 铙钹, 小锣, 堂鼓. All synthesis, no samples. Each instrument is one
 * object struck again and again: a gong already ringing is struck again, not a second gong, so its pitch, which moves
 * with how hard it rings, jumps back with each stroke as a real one does. Called from the audio interrupt only. */
#pragma once
#include <stdint.h>

#define PC_SR 44100.0f

enum { I_GU, I_BAN, I_DALUO, I_NAOBO, I_XIAOLUO, I_TANGGU, I_NPERC };   /* 板鼓 檀板 大锣 铙钹 小锣 堂鼓 */
enum {
    PS_OPEN,                                /* struck, left to ring */
    PS_LIGHT,                               /* struck lightly (令, 顷 …) */
    PS_MUTE,                                /* struck and held: a short, closed sound (闷击) */
    PS_STOP,                                /* the hand on it: no stroke, the ringing stops */
    PS_DOUBLE,                              /* both sticks together (板鼓's 八) */
    PS_ROLL,                                /* a roll (嘟, 撕边 …) from now until the next stroke or PS_STOP */
    PS_ROLLEND,                             /* the roll stops (what it struck rings on) */
    PS_NSTROKE
};

/* one mode of a struck object, as measured (tools/perc_fit.py) */
typedef struct {
    float hz;                               /* at rest: the object struck lightly, or late in its ring */
    float amp;                              /* its level after an open stroke at full velocity */
    float t60;                              /* its ring: seconds to -60 dB */
    float glide;                            /* just after a full stroke it sounds at hz * (1 + glide): a gong whose
                                             * pitch falls as it rings has glide > 0, one whose pitch rises < 0 */
    float bloom;                            /* a mode that swells after the stroke: its rise's time constant (s; 0:
                                             * none). Its envelope is then e^-t/T - (1 - direct) e^-t/bloom */
    float direct;                           /* ... and how much of it sounds at once */
} pc_mode_t;
/* a band of noise (the cymbals' wash) */
typedef struct {
    float hz, q, amp, t60;
} pc_band_t;
typedef struct {
    const pc_mode_t *mode;
    uint8_t nmode;
    const pc_band_t *band;
    uint8_t nband;
    float click, click_t, click_hz, click_hp;   /* the contact: a burst of noise, its decay (s), its lowpass and
                                             * highpass (Hz) */
    float bright;                           /* the upper modes' growth with velocity */
    float mute_t60;                         /* the hand on it: every mode's ring cut to this (s) */
    float track_t;                          /* the glide's fast part's time constant (s) */
    float slow_t, slow;                     /* its slow part (a big gong goes on sinking for seconds, as its swing dies):
                                             * time constant (s), and its share of the glide */
    float light;                            /* a light stroke's velocity, re. an open one */
    float roll_hz;                          /* a roll's strokes a second */
    float pan;                              /* -1 left .. 1 right */
    float send;                             /* to the room */
    float hp;                               /* its output's highpass (Hz; two poles): what a mallet doesn't make */
    float ring;                             /* every mode's and band's t60 times this */
    float nlp;                              /* the bands' noise lowpassed first (Hz; two poles): the highs die first */
    float floor_t;                          /* s (1/e): the bands' floor below them (a hit's low thump) dies this fast
                                             * (0: never) */
    float tail_lp;                          /* Hz (two poles): the tail's noise lowpassed again, against the rise the
                                             * zeros that take the floor away give it */
} pc_inst_t;

void pc_init(void);
void pc_hit(int inst, int stroke, float vel);   /* vel 0..1 */
void pc_panic(void);
/* n stereo frames added into l and r (about +-1), and the room's send */
void pc_render(float *l, float *r, float *send, uint32_t n, const float *level);   /* level[I_NPERC] */
float pc_ring(int inst);                    /* how loud it still rings (the tests, the screen) */
#ifdef LG_HOST
void pc_tune(int inst, const pc_inst_t *d); /* the tools: an instrument's numbers replaced (then pc_init) */
#endif
