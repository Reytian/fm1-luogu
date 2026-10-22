/* SPDX-License-Identifier: MPL-2.0 */
/* LUOGU's 文场: the 京胡 and the 京二胡 (bowed), and the 月琴 (plucked). All synthesis. A bowed voice is one string
 * under one bow: a new note on it while it sounds is a change of finger (legato, as a fiddler slurs), a slide
 * moves its pitch continuously, and its tone follows its body: each harmonic's level comes from the instrument's
 * spectral envelope at the harmonic's frequency now (hu_tab.h, measured), so vibrato and slides make the partials
 * shimmer through the body's resonances as on the real instrument. Called from the audio interrupt only. */
#pragma once
#include <stdint.h>

#define HU_SR 44100.0f

enum { HU_JINGHU, HU_ERHU, HU_NKIND };          /* the bowed instruments' kinds */
enum { HV_BAND_HU, HV_BAND_ER, HV_PLAYER, HU_NVOICE };   /* the voices: the band's two, and the player's keys */

typedef struct {
    float midi;                                 /* the pitch (MIDI note number, fractional) */
    float vel;                                  /* 0..1: the bow's weight */
    float slide_from;                           /* a slide into the note from this many semitones away (0: none) */
    float slide_s;                              /* its length (s) */
    float vib;                                  /* vibrato depth (cents; 0 none), starting after vib_delay s */
    float vib_delay;
    uint8_t legato;                             /* the same bow: no new attack */
} hu_note_t;

void hu_init(void);
void hu_panic(void);
void hu_kind(int voice, int kind);              /* which instrument a voice is (the player's: 京胡 or 二胡) */
void hu_on(int voice, const hu_note_t *n);
void hu_off(int voice, float release_s);        /* the bow lifts */
void hu_glide(int voice, float midi, float seconds);   /* the finger slides to another pitch, the bow going on */
void yq_pluck(float midi, float vel);           /* the 月琴 */
void yq_damp(void);
/* n stereo frames added into l, r, and the room's send; level[3]: 京胡 (band), 京二胡 (band), the player's voice,
 * and lv_yq the 月琴's */
void hu_render(float *l, float *r, float *send, uint32_t n, const float *level, float lv_yq);
float hu_pitch(int voice);                      /* the pitch now, -1 when silent (the tests, the screen) */
