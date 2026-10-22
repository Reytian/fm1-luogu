/* SPDX-License-Identifier: MPL-2.0 */
/* LUOGU's engine: the 锣鼓经 (the percussion's patterns) and the 过门 (the 京胡's interludes), played on time.
 *
 *   the audio interrupt (lg_render)   the transport, every stroke and note on time, the sound
 *   the main loop (lg_cmd, lg_service) the player's commands and strokes in; the practice's judging
 *
 * Two ways to play:
 *   a pattern (锣鼓): its 起 once, its 中 again and again until the player cuts it (or as often as the pattern
 *     says), then its 收: the way a 鼓师 leads it. LG_CUT asks for the 收 at the end of this 中.
 *   a 板式 to sing to (唱腔): the 锣鼓 that leads in, the 大过门, then the 板眼 kept while the player sings; LG_CUE
 *     brings in the 小过门 at the next 板 (上句 and 下句 in turn), LG_CUT the ending.
 * One instrument can be the player's (practice: 跟打): the engine leaves its strokes out and keeps when each was due,
 * and the main loop judges the player's against them. */
#pragma once
#include <stdint.h>
#include "perc.h"
#include "hu.h"

#define LG_SR 44100.0f
#define LG_TPB 48                       /* ticks a beat: halves, thirds, quarters, sixths, eighths all whole */

/* ------------------------------------------------------------------ the data (lg_data.h, tools/lg_data.py) */
typedef struct {                        /* a syllable of the 锣鼓经, and what it strikes */
    const char *zi;                     /* as written: "仓" */
    const char *py;                     /* pinyin: "cang" */
    uint8_t n;                          /* strokes */
    uint8_t st[4];                      /* each: instrument << 4 | stroke (perc.h I_*, PS_*) */
    uint8_t vel[4];                     /* 1..127 */
} lg_syl_t;

enum { LGF_ACC = 1, LGF_SOFT = 2, LGF_BAR = 4, LGF_BEAT = 8, LGF_HOLD = 16, LGF_EYE = 32, LGF_ROLL = 64, LGF_FILL = 128 };
typedef struct {
    uint16_t at;                        /* ticks from its section's start */
    uint8_t syl;                        /* LG_SYL's index (乙, a rest, and -, held, strike nothing) */
    uint8_t fl;                         /* LGF_*: accented, soft; a bar (板) or a beat starts here; held; a 眼
                                         * stroke (left out when the 板 alone is kept: lg_set.yan 0); a roll (the
                                         * drum's 嘟, the cymbals' tremolo) for its length; the 鼓师's stroke the
                                         * text leaves out (tools/lg_data.py drum_part: not shown, not owed) */
    uint8_t len;                        /* its length (ticks, at most 255): a roll's */
} lg_ev_t;

typedef struct {
    const lg_ev_t *ev;
    uint16_t nev, len;                  /* its events; its length (ticks) */
} lg_sec_t;

enum { LG_QI, LG_ZHONG, LG_SHOU };      /* a pattern's sections: 起 (once), 中 (again and again), 收 */
typedef struct {
    const char *name, *py, *use;        /* "急急风", "ji ji feng", what it is for */
    uint8_t cat;                        /* its kind (LG_CAT_NAME) */
    uint8_t meter;                      /* beats a bar: 1 (一板), 2 (一板一眼), 4 (一板三眼); 0 散板 */
    uint8_t mixed;                      /* meter 0 but in bars, of 2 beats and of 3 as they come (水底鱼) */
    uint16_t bpm, lo, hi;               /* its tempo, and the range it is played in */
    uint8_t reps;                       /* 中 played this many times when nobody cuts it (0: until cut) */
    float acc0, acc1;                   /* 散板: the tempo's factor at the start and at the end */
    lg_sec_t s[3];
} lg_pat_t;

enum { LGN_SLIDE_UP = 1, LGN_SLIDE_DOWN = 2, LGN_GRACE = 4, LGN_TRILL = 8, LGN_SLUR = 16, LGN_REST = 32,
       LGN_SLIDE_DN = 64 };                /* (UP: slides in from below; DOWN: falls away at its end; DN: slides in
                                         * from above, as 西皮's closing 1 to 6̣) */
#define LG_NOLAO (-128)
typedef struct {
    uint16_t at, len;                   /* ticks */
    int8_t p;                           /* semitones from 1 (do) in the 京胡's written octave */
    int8_t g;                           /* a grace note's (LGN_GRACE), the same */
    uint8_t fl;                         /* LGN_* */
} lg_note_t;

enum { LG_XIPI, LG_ERHUANG, LG_FAN_ERHUANG, LG_SIPING, LG_NANBANGZI, LG_NQIANG };
typedef struct {                        /* a 过门 */
    const char *name;                   /* "西皮原板·起首过门" */
    uint8_t qiang, meter;               /* meter 0: 散 (free; its lengths nominal) */
    uint16_t len;                       /* ticks */
    const lg_note_t *n;
    uint16_t nn;
    const lg_ev_t *pe;                  /* the 武场 with it (the 板 on its 板s) */
    uint16_t npe;
    uint16_t sing;                      /* where the singer comes in, ticks from its start (= len: right after) */
    int8_t lao;                         /* the 落音 it follows: semitones from 1 (LG_NOLAO: any) */
} lg_gm_t;

typedef struct {                        /* a 板式 to sing to */
    const char *name;                   /* "西皮原板" */
    const char *use;                    /* a line about it (the screen) */
    uint8_t qiang, meter;
    uint16_t bpm, lo, hi;
    int8_t intro;                       /* the 锣鼓 that leads in (a pattern; -1 none) */
    int8_t big;                         /* the 大过门 (-1 none) */
    int8_t small[2];                    /* the 小过门 after a 上句 and after a 下句, when the 落音 isn't known */
    int8_t lao[6];                      /* the 小过门 by 落音 (their own lao), -1 none: the one for the player's
                                         * last note is played */
    int8_t keep;                        /* the 板眼 while the player sings: a pattern whose 中 is played round (-1
                                         * none: 散板, the singer free) */
    int8_t keep_gm;                     /* 紧拉慢唱 (摇板): instead, a 过门 played round under the free singing */
    int8_t tail;                        /* the closing 过门 (-1 none) */
    int8_t end;                         /* the closing 锣鼓 (-1 none) */
    uint8_t gong;                       /* 1: a stroke of the 大锣 before each 小过门 (散板's 加一锣) */
} lg_bs_t;

/* ------------------------------------------------------------------ settings */
enum { LV_GU, LV_DALUO, LV_NAOBO, LV_XIAOLUO, LV_JINGHU, LV_ERHU, LV_YUEQIN, LV_KEYS, LV_N };
typedef struct {
    uint16_t bpm;                       /* the tempo now (the pattern's own when one is chosen) */
    int8_t key[LG_NQIANG];              /* the 调门 for each 声腔: 1 = this many semitones above C (0..11) */
    uint8_t vol[LV_N];                  /* 0..100 */
    uint8_t room;                       /* 0..100 */
    uint8_t keys_inst;                  /* what the keys play: LG_KEYS_* */
    uint8_t mine;                       /* the instrument the player plays (practice): 0 none, else 1 + I_* */
    uint8_t erhu, yueqin;               /* the 京二胡 and the 月琴 with the 京胡 in the 过门 */
    uint8_t yan;                        /* while singing: 0 the 板 alone (as a 鼓师 mostly plays), 1 the 眼 too */
} lg_set_t;
enum { LG_KEYS_LUOGU, LG_KEYS_JINGHU, LG_KEYS_ERHU, LG_KEYS_YUEQIN, LG_NKEYS };
extern lg_set_t lg_set;

/* ------------------------------------------------------------------ commands (the main loop) */
enum {
    LG_PLAY_PAT = 1,                    /* a: the pattern */
    LG_PLAY_BS,                         /* a: the 板式 */
    LG_CUT,                             /* the 收 (a pattern: at the end of this 中; singing: the ending) */
    LG_CUE,                             /* singing: the next 小过门, at the next 板; a: 1 + the 落音's pitch class
                                         * from 1 (the player's last note), 0 not known */
    LG_STOP,                            /* now (what rings rings on) */
    LG_HIT,                             /* a: instrument, b: stroke, c: velocity 0..127 (a key, MIDI) */
    LG_NOTE,                            /* a: MIDI note (0: the keys' voice off), c: velocity */
    LG_KARA,                            /* karaoke (跟练): a key struck; a: the syllable it names (LG_SYL's, 255 none),
                                         * b: the stroke it plays (instrument << 4 | stroke; 255: a whole syllable's) */
    LG_PLAY_TQ,                         /* a: the 唱段 (lg_tq) */
    LG_SAY,                             /* the 彩蛋: a: the clip (0 包, 1 肉包; lg_nsay), from its start */
};
void lg_cmd(uint32_t type, uint32_t a, uint32_t b, uint32_t c);
/* karaoke (跟练, set by the app): in a pattern of the 锣鼓经 the time follows the player's strokes (every syllable, or
 * with lg_set.mine those of their instrument): it stops at each till they strike it (LG_KARA), and a key struck before
 * its time that is the next stroke's moves the time on to it at once. So the 字谱 lights as they go, and a 板式's 过门
 * comes in only once its lead-in is played. A key struck with no stroke owed left in a section is for the next
 * section's first. The 板 under the singing and the 过门 don't wait */
extern volatile uint8_t lg_kara;
/* karaoke: event e is the player's to strike, for a player of instrument mine (-1: all of them) */
int lg_kara_owes(const lg_ev_t *e, int mine);
/* karaoke: a key (LG_KARA's a, b) plays syllable syl, for a player of instrument mine (-1: all of them) */
int lg_kara_fits(int syl, uint32_t a, uint32_t b, int mine);

void lg_init(void);
void lg_defaults(lg_set_t *s);
void lg_time(uint32_t ms);              /* audio interrupt, before lg_render: the block's start (ms) */
void lg_render(int32_t *lr, uint32_t n, uint32_t gain_q12);   /* audio interrupt: n stereo frames, 24-bit */
void lg_panic(void);
void lg_service(uint32_t now_ms);       /* main loop, every pass: the practice's judging */
void lg_player_hit(int inst, uint32_t ms);   /* main loop: the player struck their instrument (practice) */

/* ------------------------------------------------------------------ 唱段: arias with their 托腔 (tq_data.h, made by
 * tools/tq_data.py from the Jingju Music Scores Collection, under its CC BY-NC-ND 4.0; none in a build without it) */
typedef struct {                        /* a section of a 唱段: one 板式 */
    const char *bs;                     /* LUOGU's 板式, by name ("西皮原板"): its tempo, its 锣鼓 */
    uint8_t meter, pick;                /* beats a bar; a pickup: beats before its first 板 (the 京胡 in on a 眼) */
    uint8_t bpm;                        /* its tempo: quarter notes a minute (K1 moves a 唱段's together) */
    uint32_t at;                        /* where it starts in the 唱段 (ticks) */
    uint16_t len;                       /* ticks */
    uint32_t off;                       /* its notes in TQ_NOTES, packed (tools/tq_data.py) ... */
    uint16_t nn;                        /* ... this many */
} lg_tq_sec_t;
typedef struct { uint16_t at, glyph; uint8_t dim; } lg_tq_syl_t;   /* a sung syllable: when (ticks into the 唱段), its
                                                                   * character, an interjection (（哇）: dim) */
typedef struct { uint16_t y0; uint8_t n, typ; } lg_tq_line_t;      /* a line: its syllables; 0 上句, 1 下句 */
typedef struct {                        /* a 唱段 */
    const char *play, *first, *role;    /* "空城计", its first line, who sings it */
    uint8_t qiang;                      /* LG_XIPI ... */
    uint8_t s0, ns;                     /* its sections */
    uint16_t l0, nl;                    /* its lines */
} lg_tq_t;
int lg_ntq(void);
const lg_tq_t *lg_tq(int i);
const lg_tq_sec_t *lg_tq_sec(int i);
int lg_tq_bs(int i);                    /* section i's 板式 (LG_BS's), -1 none of that name */
const lg_tq_line_t *lg_tq_line(int i);
const lg_tq_syl_t *lg_tq_syl(int i);
const uint8_t *lg_tq_glyph(int i);      /* a character: 12 x 12, a bit a pixel, rows top to bottom (18 bytes) */
int lg_tq_glyph_of(uint32_t code);      /* a character's glyph (by its code point), -1 none */

/* the 彩蛋 (郭德纲模式): 郭德纲's sung 包 and 肉包 from his and 于谦's 《黄鹤楼》 (guo_snd.h, made by tools/guo.py; 德云社's
 * recording, not under LUOGU's licence; none in a build without it) */
int lg_nsay(void);

/* ------------------------------------------------------------------ what is playing (the screen, the tests) */
enum { LG_IDLE, LG_PATTERN, LG_SINGING };
enum { SEG_NONE, SEG_PAT, SEG_GM, SEG_KEEP };
typedef struct {
    uint8_t state;                      /* LG_IDLE, LG_PATTERN, LG_SINGING */
    uint8_t seg;                        /* SEG_*: what plays now */
    int8_t pat, bs, gm;                 /* the pattern (or the keep's), the 板式, the 过门 */
    uint8_t sec;                        /* the pattern's section */
    int16_t ev;                         /* the event that sounded last in it (-1 none yet) */
    int16_t note;                       /* the 过门's note sounding (-1 none) */
    uint16_t rep;                       /* times round the 中 so far */
    uint8_t cut, cue;                   /* a 收 asked for; a 小过门 asked for */
    int8_t cue_pc;                      /* ... after this 落音 (pitch class from 1; -1 not known) */
    uint8_t sing;                       /* singing: the singer's part (past a 过门's [唱], or in the keep) */
    uint8_t half;                       /* singing: 0 a 上句 next, 1 a 下句 */
    int32_t beat;                       /* beats since the start (the 板眼: beat % meter == 0 is a 板) */
    float frac;                         /* ... and how far into the next */
    uint8_t meter;
    uint8_t wait;                       /* karaoke: the time waits for the player's stroke ... */
    int16_t want;                       /* ... this event's; not waiting, the next one owed in this section (-1 none) */
    uint16_t kara_hits, kara_miss;      /* karaoke: strokes struck right, keys struck wrong (counting on and on: the
                                         * app takes a run's) */
    int8_t tq;                          /* the 唱段 playing (-1 none: a 板式, or a pattern) */
    uint8_t tq_sec;                     /* ... its section (its 板式) */
    uint16_t tq_at;                     /* ... where its 京胡 is: ticks from its start (the lyrics' clock) */
    int8_t say;                         /* the 彩蛋's clip sounding (LG_SAY; -1 none) */
    uint32_t serial;                    /* changes whenever any of these does: the screen redraws */
} lg_now_t;
extern volatile lg_now_t lg_now;

/* the practice: what the player got (main loop) */
enum { J_NONE, J_GOOD, J_EARLY, J_LATE, J_MISS, J_EXTRA };
typedef struct {
    uint16_t due, good, early, late, miss, extra;
    uint8_t last;                       /* J_*: the last judgement */
    int16_t last_ms;                    /* ... how far off (ms, + late) */
    uint32_t last_at;                   /* when */
} lg_score_t;
extern lg_score_t lg_score;
void lg_score_reset(void);

/* the data, for the screen (lg_data.h's tables) */
const lg_syl_t *lg_syl(int i);
const lg_pat_t *lg_pat(int i);
const lg_gm_t *lg_gm(int i);
const lg_gm_t *lg_cur_gm(void);         /* the 过门 sounding (LG_GM's or a 唱段's section), 0 none */
const lg_bs_t *lg_bs(int i);
const char *lg_cat_name(int i);
int lg_do_midi(int qiang);              /* the 京胡's written 1 in the 调门 now (MIDI) */
int lg_nsyl(void);
int lg_npat(void);
int lg_ngm(void);
int lg_nbs(void);
int lg_ncat(void);
