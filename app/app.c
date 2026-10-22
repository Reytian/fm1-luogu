/* SPDX-License-Identifier: MPL-2.0 */
/* LUOGU on fm1-base: the screens, the controls, the settings, and the glue to the engine (dsp/lg.c).
 * Part of fm1-base's unity build (device/unity.c, host/plat_host.c), after the platform: gfx.c, announce.c and
 * touch.c are in scope. (Its shape is fm1-base's template's, by Charles Vestal.)
 *
 *   锣鼓 (HOME)      a pattern of the 锣鼓经: SELECT or ALGO picks it, PRESET its kind; PLAY starts it, PLAY again
 *                    plays its 收 (the ending) and it stops, held: it stops now. ARP: round and round. The 字谱 lights
 *                    as it sounds.
 *                    K1 速度 (the tempo)  K2 乐器 (practice: the instrument the player plays; its strokes are left out
 *                    and the player's judged)  K3 跟练 (as FX)  K4 键盘 (what the keys play)
 *                    REC: practice from the start (the score at the end)
 *   FX               跟练 (karaoke), either page: the 锣鼓经's time follows the player's strokes (lg.h lg_kara): it
 *                    waits at each till its key, and moves on at once to one struck early; the 字谱 turning yellow
 *                    as they go; a 板式's 过门 once its lead-in is played. Stopped, the first stroke's key starts it
 *   SEL              the keys' map, either page: each key's syllable, coloured by player, lit while held
 *   唱腔 (HOME)      a 板式 to sing to: SELECT or ALGO picks it; PLAY: the 锣鼓 leads in, the 大过门 (its 简谱 lit as
 *                    it sounds), then the 板眼 while the player sings; SEQ brings in the 小过门 at the next 板 (the
 *                    one for the 落音 just played on the keys, if any); PLAY again: the ending.
 *                    K1 速度  K2 调门  K3 板眼 (the 板 alone, or the 眼 too)  K4 键盘
 *   混音 (GLO)       the levels: ALGO the row (武场, 文场, 其他), K1 .. K4 its four
 *   the keys         the 武场's strokes (each key a stroke of one instrument, the top ones a whole syllable), or a
 *                    京胡, 京二胡 or 月琴 in the 调门 (white keys 1 2 3 4 5 6 7 from C, as 简谱)
 *   OCT- / OCT+      the keys an octave down or up; SAVE keeps the settings; EDIT: what the pattern is for */
#include "fastmath.h"
#include "lg.h"
#include "zh.c"

#define SET_MAGIC 0x4C474F31u            /* "LGO1": the settings record; another magic loads defaults */
typedef struct {
    uint32_t magic;
    lg_set_t lg;
    uint8_t page;                        /* the page at power-on: 锣鼓 or 唱腔 */
    uint8_t pat, bs;                     /* the pattern and the 板式 chosen */
    int8_t octave;                       /* the keys: -1 .. +1 */
    uint8_t loop;                        /* ARP: a pattern round and round */
    uint8_t click;                       /* (unused; kept for the record's layout) */
    uint8_t kara;                        /* karaoke (跟练): the time follows the player's strokes (FX) */
    uint8_t rsv;
} settings_t;
static settings_t set;
static uint8_t set_dirty;                /* changed since the last save: SAVE is lit */

static int same(const char *a, const char *b)   /* (the device's libc has no strcmp) */
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

static uint32_t len_of(const char *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}

/* ------------------------------------------------------------------ the engine's glue */
/* the keys as the 武场: a stroke of one instrument each (the low ones), or a whole syllable (the top ones, by name).
 * Each player's strokes run as their hands do, left to right: the 鼓师's 板 (left hand), both sticks, the right stick;
 * the gongs' hand on the gong (止), struck while held (闷), then the mallet open and light; the cymbals pressed (扑),
 * open, trembling. The players in the order of the mix page: 鼓师, 大锣, 铙钹, 小锣 */
typedef struct {
    int8_t inst, stroke;                 /* I_*, PS_* (-1: a syllable instead) */
    const char *syl;
} pkey_t;
static const pkey_t PKEY[NKEYS] = {
    {I_BAN, PS_OPEN, 0},      {I_BAN, PS_LIGHT, 0},     {I_GU, PS_DOUBLE, 0},     {I_GU, PS_ROLL, 0},   /* F3 扎 衣 八 嘟 */
    {I_GU, PS_OPEN, 0},       {I_GU, PS_LIGHT, 0},      {I_TANGGU, PS_OPEN, 0},                      /* A3 大 多, B3 堂鼓 */
    {I_DALUO, PS_STOP, 0},    {I_DALUO, PS_MUTE, 0},    {I_DALUO, PS_OPEN, 0},    {I_DALUO, PS_LIGHT, 0},  /* C4 止 宫 匡 空 */
    {I_NAOBO, PS_MUTE, 0},    {I_NAOBO, PS_OPEN, 0},    {I_NAOBO, PS_ROLL, 0},                          /* E4 扑 七, F#4 颤 */
    {I_XIAOLUO, PS_STOP, 0},  {I_XIAOLUO, PS_MUTE, 0},  {I_XIAOLUO, PS_OPEN, 0},  {I_XIAOLUO, PS_LIGHT, 0},  /* G4 止 匝 台 令 */
    {I_TANGGU, PS_ROLL, 0},                                                                          /* B4 堂鼓 滚 */
    {-1, 0, "仓"}, {-1, 0, "顷"}, {-1, 0, "才"}, {-1, 0, "匡"}, {-1, 0, "台"}, {-1, 0, "七"},           /* C5 .. F5 */
    {-1, 0, "令"}, {-1, 0, "扎"}};                                                                       /* F#5, G5 */
/* what each key is, for the screen (the low keys: the stroke's syllable; 堂 the 堂鼓) */
static const char *const PKEY_NAME[NKEYS] = {"扎", "衣", "八", "嘟", "大", "多", "堂", "止", "宫", "匡", "空", "扑", "七",
                                             "颤", "止", "匝", "台", "令", "滚", "仓", "顷", "才", "匡", "台", "七", "令",
                                             "扎"};
static int8_t pkey_syl[NKEYS];           /* the top keys' syllables, found by name (-1: none) */
static int8_t pkey_named[NKEYS];         /* every key's: the syllable its label is (karaoke: 匡 on D4; -1 none) */

static int note_midi(int note);          /* (the keys' and MIDI's note in the 调门: below) */
static volatile int midi_pc = -1;        /* the last melodic note in over MIDI: its place from C, and when */
static volatile uint32_t midi_at;
static volatile uint32_t midi_key[16], midi_key_at[16];   /* MIDI's strokes for the main loop: key | velocity << 8 |
                                                           * (sounded already) << 16, and when; a ring */
static volatile uint32_t midi_key_w, midi_key_r;

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {  /* a keyboard or pads over USB or the TRS jack: as the keys, by note */
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu, type = st & 0xF0u;
        int k = (int)d1 - 53 - 12 * set.octave;
        if (type != 0x90u && type != 0x80u)
            continue;
        if (lg_set.keys_inst != LG_KEYS_LUOGU) {
            int m = note_midi((int)d1 - 12 * set.octave);
            lg_cmd(LG_NOTE, (uint32_t)m, 0, type == 0x90u ? d2 : 0u);
            if (type == 0x90u && d2) {           /* (the 落音 the next 小过门 follows) */
                midi_pc = (int)((d1 + 120u) % 12u);
                midi_at = plat_ms();
            }
        } else if (type == 0x90u && d2 && k >= 0 && k < NKEYS) {
            if (PKEY[k].inst >= 0)               /* a single stroke sounds at once; the main loop judges it (and
                                                  * plays a whole syllable) */
                lg_cmd(LG_HIT, (uint32_t)PKEY[k].inst, (uint32_t)PKEY[k].stroke, d2);
            if (midi_key_w - midi_key_r < 16u) {
                midi_key[midi_key_w & 15u] = (uint32_t)k | d2 << 8 | (PKEY[k].inst >= 0 ? 1u << 16 : 0u);
                midi_key_at[midi_key_w & 15u] = plat_ms();
                midi_key_w++;
            }
        }
    }
    lg_time(plat_ms());
    lg_render(lr, n, master_q12);
}

void app_panic(void) { lg_panic(); }

/* --------------------------------------------------------------- settings --- */
static void settings_defaults(void)
{
    set.magic = SET_MAGIC;
    lg_defaults(&set.lg);
    set.page = 0;
    set.pat = 0;
    set.bs = 0;
    set.octave = 0;
    set.loop = 0;
    set.click = 70;
    set.kara = 0;
}

static int settings_ok(void)
{
    const lg_set_t *g = &set.lg;
    int i;
    for (i = 0; i < LG_NQIANG; i++)
        if (g->key[i] < 0 || g->key[i] > 11)
            return 0;
    if (set.magic != SET_MAGIC || g->bpm < 30u || g->bpm > 320u || g->room > 100u ||
        g->keys_inst >= LG_NKEYS || g->mine > I_NPERC || g->erhu > 1u || g->yueqin > 1u || g->yan > 1u || set.page > 1u ||
        set.pat >= lg_npat() || set.octave < -1 || set.octave > 1 ||
        set.loop > 1u || set.click > 100u || set.kara > 1u)
        return 0;
    for (i = 0; i < LV_N; i++)
        if (g->vol[i] > 100u)
            return 0;
    return 1;
}

static void apply(void)                  /* a setting changed: into the engine, and SAVE lit */
{
    lg_set = set.lg;
    lg_set.vol[LV_GU] = set.lg.vol[LV_GU];
    lg_kara = set.kara;
    set_dirty = 1;
}

void app_init(void)
{
    int k;
    lg_init();
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || !settings_ok())
        settings_defaults();
    if (set.bs >= lg_nbs() + lg_ntq())         /* (a 唱段 this build has not: the first 板式) */
        set.bs = 0;
    lg_set = set.lg;
    lg_kara = set.kara;
    for (k = 0; k < NKEYS; k++) {
        int i;
        pkey_syl[k] = pkey_named[k] = -1;
        for (i = 0; i < lg_nsyl(); i++) {
            if (PKEY[k].syl && same(lg_syl(i)->zi, PKEY[k].syl))
                pkey_syl[k] = (int8_t)i;
            if (same(lg_syl(i)->zi, PKEY_NAME[k]))
                pkey_named[k] = (int8_t)i;
        }
    }
}

void app_loop(void) { lg_service(plat_ms()); }

/* --------------------------------------------------------------------- UI --- */
enum { PG_LUOGU, PG_SING, PG_MIX };
static const char *const PAGE_NAME[3] = {"锣鼓", "唱腔", "混音"};
static const char *const KEYS_NAME[LG_NKEYS] = {"锣鼓", "京胡", "京二胡", "月琴"};
static const char *const MINE_NAME[I_NPERC + 1] = {"全部", "鼓板", "板", "大锣", "铙钹", "小锣", "堂鼓"};
static const char *const KEY_NAME[12] = {"C", "#C", "D", "bE", "E", "F", "#F", "G", "bA", "A", "bB", "B"};
static const char *const METER_NAME[5] = {"散板", "有板无眼", "一板一眼", "一板两眼", "一板三眼"};
static const char *const MIX_NAME[3][4] = {{"鼓板", "大锣", "铙钹", "小锣"}, {"京胡", "京二胡", "月琴", "键盘"},
                                           {"混响", "键盘", "京二胡", "月琴"}};
#define PLAY_HOLD_MS 600u                /* PLAY held this long: stop now */

static struct {
    char msg[2][32];                     /* a short message over the header */
    uint8_t msg_px;                      /* ... its second part a 唱段's words: in their pixel font (app_say_tq) */
    uint32_t msg_until;
    uint32_t keys, btn;                  /* held at the last input */
    uint8_t page, mix_row;
    uint8_t redraw;                      /* bit 0 header, 1 title, 2 body, 3 footer */
    int turned;                          /* the knob turned last (EN_*): lit a moment */
    uint32_t turned_at;
    uint32_t play_at;                    /* PLAY went down (0: up, or its hold has acted) */
    uint32_t serial;                     /* the engine's, at the last draw */
    int32_t shown_beat;
    uint8_t practising;                  /* REC: a scored run under way */
    uint8_t show_use;                    /* EDIT: what the pattern is for, over the 字谱 */
    uint16_t judged;                     /* judgements shown so far */
    int note_shown;                      /* the keys' last note: its place from C (its 简谱), -1 none */
    uint32_t note_at;                    /* ... when: the 落音 a 小过门 follows, if lately */
    int8_t key_down;                     /* the last key pressed */
    int16_t jp_note;                     /* the 过门's note sounding, or the last one (through its rests) */
    int8_t jp_gm;                        /* ... in this 过门 */
    uint32_t intro_at, intro_drawn;      /* the logo's intro: when it began (0: over), its last frame */
    int16_t tq_syl;                      /* a 唱段's syllable sung now, as last drawn (-1 none yet) */
    uint8_t touched;                     /* a key, a button or a knob since the intro began (it ends it) */
    uint8_t show_keys;                   /* SEL: the keys' map in the body */
    uint16_t kara_hits, kara_miss;       /* karaoke: the strokes right, and the wrong keys, this run */
    uint16_t kara_seen[2];               /* ... the engine's counts of them taken so far (lg_now.kara_hits, kara_miss) */
    uint8_t kara_run;                    /* ... a run under way (its tally at the end) */
    uint8_t guo;                         /* the 郭德纲模式 (a 彩蛋: LFO twice on 西皮慢板) */
    uint32_t lfo_at;                     /* ... LFO's last press (0: none lately) */
    int8_t say_shown;                    /* ... the engine's clip as last drawn (lg_now.say) */
    int8_t bubble;                       /* ... the clip in his bubble, and till when it stays after it (0: gone) */
    uint32_t bubble_until;
} ui;

static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}

static void int_text(int32_t v, char *b)     /* signed, no padding */
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

static void copy_utf8(char *d, const char *s, uint32_t max)   /* at most max bytes, never half a character */
{
    uint32_t i = 0;
    while (s && s[i] && i < max - 1u) {
        uint32_t len = ((uint8_t)s[i] & 0xE0u) == 0xC0u ? 2u : ((uint8_t)s[i] & 0xF0u) == 0xE0u ? 3u : 1u;
        if (i + len > max - 1u)
            break;
        while (len--) {
            d[i] = s[i];
            i++;
        }
    }
    d[i] = 0;
}

void app_say(const char *a, const char *b);

static void app_say_tq(const char *a, const char *b)   /* a message whose second part is a 唱段's words */
{
    app_say(a, b);
    ui.msg_px = 1;
}

void app_say(const char *a, const char *b)
{
    char t[72];
    copy_utf8(ui.msg[0], a, sizeof ui.msg[0]);
    copy_utf8(ui.msg[1], b, sizeof ui.msg[1]);
    ui.msg_px = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw |= 1u;
    if (ann_on()) {                          /* and to a screen reader (announce.c, fm1-reader) */
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

static void say_value(const char *name, int32_t v, const char *unit)
{
    char t[16];
    int_text(v, t);
    text_cat(t, unit);
    app_say(name, t);
}

static void key_name(int key, char *t)       /* the 调门: "1=E" */
{
    t[0] = 0;
    text_cat(t, "1=");
    text_cat(t, KEY_NAME[clamp(key, 0, 11)]);
}

/* the 唱腔 page's choice (set.bs): one of the 板式, or after them a 唱段 (lg_tq: an aria with its 托腔) */
static int sel_tq(void) { return set.bs >= lg_nbs() && set.bs < lg_nbs() + lg_ntq() ? set.bs - lg_nbs() : -1; }

static int sel_bs(void)                  /* the 板式 it is sung in (a 唱段: its first section's) */
{
    int t = sel_tq(), b;
    if (t < 0)
        return set.bs < lg_nbs() ? set.bs : 0;
    b = lg_tq_bs(lg_tq(t)->s0);
    return b >= 0 ? b : 0;
}

static uint16_t sel_bpm(void)            /* its tempo (a 唱段's: its first section's, quarter notes a minute) */
{
    int t = sel_tq();
    return t >= 0 ? lg_tq_sec(lg_tq(t)->s0)->bpm : lg_bs(sel_bs())->bpm;
}

static const char *sel_name(void)
{
    int t = sel_tq();
    return t >= 0 ? lg_tq(t)->first : lg_bs(sel_bs())->name;
}

static int cur_qiang(void)               /* the 声腔 sung now (the 板式 or 唱段 chosen), 西皮 off the 唱腔 page */
{
    if (ui.page != PG_SING || !lg_nbs())
        return LG_XIPI;
    return sel_tq() >= 0 ? lg_tq(sel_tq())->qiang : lg_bs(sel_bs())->qiang;
}

/* ------------------------------------------------------------------ transport */
static int playing(void) { return lg_now.state != LG_IDLE; }
/* a 板式 with 小过门 between its lines (导板 and 摇板 have none: one line, or the 京胡 going on under the singing) */
static int has_small(const lg_bs_t *b) { return b->small[0] >= 0 || b->small[1] >= 0 || b->lao[0] >= 0; }

static void start(void)
{
    ui.kara_hits = ui.kara_miss = 0;         /* (karaoke: the tally from here) */
    ui.kara_run = set.kara;
    if (ui.page == PG_SING) {
        if (!lg_nbs())
            return;
        lg_set.bpm = set.lg.bpm;
        if (sel_tq() >= 0) {
            lg_cmd(LG_PLAY_TQ, (uint32_t)sel_tq(), 0, 0);
            app_say_tq("唱段", lg_tq(sel_tq())->first);
        } else {
            lg_cmd(LG_PLAY_BS, set.bs, 0, 0);
            app_say("唱腔", lg_bs(set.bs)->name);
        }
    } else {
        lg_cmd(LG_PLAY_PAT, set.pat, 0, 0);
        app_say("起", lg_pat(set.pat)->name);
    }
}

static void play_button(void)            /* PLAY: start; playing: the 收; in the 收 already: stop */
{
    if (!playing()) {
        ui.practising = 0;
        start();
    } else if (lg_now.cut || (lg_now.seg == SEG_PAT && lg_now.sec == LG_SHOU && lg_now.state == LG_PATTERN)) {
        lg_cmd(LG_STOP, 0, 0, 0);
        app_say("停", "");
    } else {
        lg_cmd(LG_CUT, 0, 0, 0);
        app_say("收", "");
    }
}

static void choose(int d)                /* ALGO: the pattern, or in its group (PRESET's) the 板式 or the 唱段 */
{
    if (ui.page == PG_SING) {
        int lo = sel_tq() >= 0 ? lg_nbs() : 0, n = sel_tq() >= 0 ? lg_ntq() : lg_nbs();
        if (!n)
            return;
        set.bs = (uint8_t)(lo + (set.bs - lo + d + n * 8) % n);
        set.lg.bpm = sel_bpm();
        apply();
        if (sel_tq() >= 0)
            app_say_tq("唱段", sel_name());
        else
            app_say(sel_name(), METER_NAME[lg_bs(set.bs)->meter <= 4 ? lg_bs(set.bs)->meter : 0]);
    } else {
        int n = lg_npat();
        set.pat = (uint8_t)((set.pat + d + n * 8) % n);
        set.lg.bpm = lg_pat(set.pat)->bpm;
        apply();
        app_say(lg_pat(set.pat)->name, lg_cat_name(lg_pat(set.pat)->cat));
    }
    ui.show_use = 0;
    ui.redraw |= 14u;                        /* (the footer's tempo too) */
}

static void choose_group(void)           /* PRESET on the 唱腔 page: the 板式, or the 唱段 */
{
    if (!lg_ntq())
        return;
    set.bs = (uint8_t)(sel_tq() >= 0 ? 0 : lg_nbs());
    set.lg.bpm = sel_bpm();
    apply();
    if (sel_tq() >= 0)
        app_say_tq("唱段", sel_name());
    else
        app_say("板式", sel_name());
    ui.redraw |= 14u;
}

static void choose_cat(int d)            /* PRESET: the first pattern of the next (or the last) kind */
{
    int n = lg_npat(), c = lg_pat(set.pat)->cat, i, k = set.pat;
    for (i = 0; i < n; i++) {
        k = (k + d + n) % n;
        if (lg_pat(k)->cat != c)
            break;
    }
    if (d < 0)                           /* back: the kind's first */
        while (lg_pat((k - 1 + n) % n)->cat == lg_pat(k)->cat && (k - 1 + n) % n != set.pat)
            k = (k - 1 + n) % n;
    set.pat = (uint8_t)k;
    set.lg.bpm = lg_pat(k)->bpm;
    apply();
    app_say(lg_cat_name(lg_pat(k)->cat), lg_pat(k)->name);
    ui.redraw |= 14u;
}

/* ------------------------------------------------------------------ the keys */
static int note_midi(int note)           /* a key's (or a MIDI note's) note in the 调门: middle C sounds the 京胡's 1,
                                          * the 京二胡 and the 月琴 an octave lower */
{
    int m = lg_do_midi(cur_qiang()) + note - 60 + 12 * set.octave;
    if (lg_set.keys_inst != LG_KEYS_JINGHU)
        m -= 12;
    return clamp(m, 1, 127);
}

static int key_midi(int k) { return note_midi(53 + k); }

/* karaoke: key k as the engine judges it (LG_KARA): the syllable its label is, and the stroke it plays (255: none, a
 * whole syllable's key) */
static void kara_key(int k, uint32_t *a, uint32_t *b)
{
    *a = pkey_named[k] >= 0 ? (uint32_t)pkey_named[k] : 255u;
    *b = PKEY[k].inst < 0 ? 255u : (uint32_t)(PKEY[k].inst << 4 | PKEY[k].stroke);
}

/* 跟练, stopped: the pattern a run starts with on this page (the 板式's lead-in on 唱腔; 0 none) */
static const lg_pat_t *kara_run_pat(void)
{
    if (ui.page == PG_LUOGU)
        return lg_pat(set.pat);
    if (ui.page == PG_SING && lg_nbs() && lg_bs(sel_bs())->intro >= 0)
        return lg_pat(lg_bs(sel_bs())->intro);
    return 0;
}

/* 跟练: the first stroke of p the player owes (its section in *sec), -1 none */
static int kara_first(const lg_pat_t *p, int *sec)
{
    int s, i, mine = lg_set.mine ? (int)lg_set.mine - 1 : -1;
    for (s = 0; s < 3; s++)
        for (i = 0; i < (int)p->s[s].nev; i++)
            if (lg_kara_owes(&p->s[s].ev[i], mine)) {
                *sec = s;
                return i;
            }
    *sec = -1;
    return -1;
}

/* 跟练: the syllable the player strikes next (the one the time waits at or comes to next; stopped, the one a run
 * starts with), -1 none */
static int kara_target(void)
{
    const lg_pat_t *p;
    int sec, i;
    if (playing()) {
        if (lg_now.seg != SEG_PAT || lg_now.want < 0 || lg_now.pat < 0)
            return -1;
        p = lg_pat(lg_now.pat);
        return lg_now.want < (int)p->s[lg_now.sec].nev ? p->s[lg_now.sec].ev[lg_now.want].syl : -1;
    }
    p = kara_run_pat();
    if (!p || (i = kara_first(p, &sec)) < 0)
        return -1;
    return p->s[sec].ev[i].syl;
}

/* 跟练: key k strikes the syllable the player strikes next (lg_kara_fits: its own key, or the key of its one stroke,
 * 匡 also on its 大锣 key so named; with 乐器, the player's instrument's key for its stroke there) */
static int key_fits(int k)
{
    uint32_t a, b;
    int syl = kara_target();
    if (syl < 0)
        return 0;
    kara_key(k, &a, &b);
    return lg_kara_fits(syl, a, b, lg_set.mine ? (int)lg_set.mine - 1 : -1);
}

static void key_note_name(int k, char *t)  /* "C5" */
{
    static const char *const N[12] = {"C", "#C", "D", "#D", "E", "F", "#F", "G", "#G", "A", "#A", "B"};
    t[0] = 0;
    text_cat(t, N[(k + 5) % 12]);
    int_text((k + 5) / 12 + 3, t + len_of(t));
}

/* 跟练: pre, the syllable the player strikes next and a key that plays it ("该打 仓 C5"; "" none) */
static void kara_want(char *t, const char *pre)
{
    int j, syl = kara_target();
    char n[6];
    t[0] = 0;
    if (syl < 0)
        return;
    text_cat(t, pre);
    text_cat(t, lg_syl(syl)->zi);
    for (j = 0; j < NKEYS && !key_fits(j); j++)
        ;
    if (j < NKEYS) {
        key_note_name(j, n);
        text_cat(t, " ");
        text_cat(t, n);
    }
}

/* ------------------------------------------------------------------ the 郭德纲模式, a 彩蛋: on the 唱腔 page with
 * 西皮慢板, LFO twice. 郭德纲 and 于谦 as they stood in their 《黄鹤楼》 take the title and the body (guo_pic.h), F5 and
 * G5 sing his 包 and 肉包 from it (guo_snd.h), and a bubble over his cap says which. Both made by tools/guo.py (the
 * clips are 德云社's, not under LUOGU's licence: CREDITS.md); without them there is no 彩蛋 */
#if __has_include("guo_pic.h")
#include "guo_pic.h"
#endif
#ifndef GUO_W
#define GUO_W 0
#endif
#define GUO_KEY_BAO 24                   /* F5 */
#define GUO_KEY_ROU 26                   /* G5 */
#define GUO_TWICE_MS 600u                /* LFO pressed again within this: in, or out */
#define GUO_HOLD_MS 300u                 /* the bubble stays this long after the clip */

static int guo_built(void) { return GUO_W && lg_nsay() >= 2; }
static int guo_shown(void) { return guo_built() && ui.guo && ui.page == PG_SING; }
static int guo_said(void) { return lg_now.say >= 0 ? lg_now.say : ui.bubble_until ? ui.bubble : -1; }

#if GUO_W
static void guo_px(int32_t x, int32_t y, int32_t r0, int32_t nr, uint16_t c)   /* a pixel of the picture's, inside
                                                                                * rows r0 .. r0 + nr (the canvas) */
{
    uint16_t *d, p = swap16(c);
    if (x < 0 || x >= GUO_W || y < r0 || y >= r0 + nr)
        return;
    d = &cv_px[(uint32_t)(2 * (y - r0)) * 240u + 2u * (uint32_t)x];
    d[0] = d[1] = d[240] = d[241] = p;
}

static void guo_draw(int32_t r0, int32_t nr)   /* the picture's rows r0 .. r0 + nr, each pixel 2 x 2, into the canvas */
{
    uint32_t i = 0, at = 0, end = (uint32_t)(r0 + nr) * GUO_W;
    int32_t say = guo_said(), x, y, k;
    while (at < end && i < sizeof GUO_RLE) {
        uint32_t run = (GUO_RLE[i] >> 4) + 1u;
        uint16_t c = GUO_PAL[GUO_RLE[i] & 15u];
        i++;
        if (at + run <= (uint32_t)r0 * GUO_W) {
            at += run;
            continue;
        }
        for (; run; run--, at++)
            guo_px((int32_t)(at % GUO_W), (int32_t)(at / GUO_W), r0, nr, c);
    }
    if (say < 0)
        return;
    for (y = 0; y < 3; y++)                  /* his mouth open, */
        for (x = y == 1 ? 0 : 1; x < (y == 1 ? 5 : 4); x++)
            guo_px(GUO_MOUTH_X + x, GUO_MOUTH_Y + y, r0, nr, GUO_PAL[GUO_K]);
    {                                        /* and over his cap a bubble: 包, or 肉包 */
        int32_t n = say ? 2 : 1, bw = 12 * n + 6, bh = 18, x0 = GUO_X - bw / 2;
        for (y = 0; y < bh; y++)
            for (x = 0; x < bw; x++)
                if (!((x == 0 || x == bw - 1) && (y == 0 || y == bh - 1)))
                    guo_px(x0 + x, y, r0, nr,
                           GUO_PAL[x == 0 || y == 0 || x == bw - 1 || y == bh - 1 ? GUO_K : GUO_W_]);
        for (k = 0; k < 3; k++) {            /* (its tail, down to the cap) */
            for (x = GUO_X - 2 + k; x < GUO_X + 2 - k; x++)
                guo_px(x, bh - 1 + k, r0, nr, GUO_PAL[GUO_W_]);
            guo_px(GUO_X - 3 + k, bh - 1 + k, r0, nr, GUO_PAL[GUO_K]);
            guo_px(GUO_X + 2 - k, bh - 1 + k, r0, nr, GUO_PAL[GUO_K]);
        }
        for (k = 0; k < n; k++) {
            const uint8_t *g = GUO_GLYPH[say && k == 0 ? 1 : 0];   /* (肉, then 包) */
            for (y = 0; y < 12; y++)
                for (x = 0; x < 12; x++)
                    if (g[(y * 12 + x) >> 3] >> (7 - (y * 12 + x) % 8) & 1)
                        guo_px(x0 + 3 + 12 * k + x, 3 + y, r0, nr, GUO_PAL[GUO_K]);
        }
    }
}
#else
static void guo_draw(int32_t r0, int32_t nr) { (void)r0; (void)nr; }
#endif

static void guo_lfo(uint32_t now)        /* LFO: twice in a moment on 西皮慢板, in; twice again, out */
{
    if (!guo_built() || ui.page != PG_SING)
        return;
    if (!ui.lfo_at || now - ui.lfo_at >= GUO_TWICE_MS) {
        ui.lfo_at = now | 1u;
        return;
    }
    ui.lfo_at = 0;
    if (ui.guo) {
        ui.guo = 0;
        app_say("郭德纲", "下场");
    } else if (sel_tq() < 0 && same(lg_bs(sel_bs())->name, "西皮慢板")) {
        ui.guo = 1;
        app_say("郭德纲", "F5 包 · G5 肉包");
    } else {
        return;
    }
    ui.redraw |= 15u;
}

/* a key (or a MIDI note as one) went down: its stroke (unless MIDI sounded it already), the practice's judging, and in
 * karaoke whether it is the stroke waited for */
static void key_down(int k, uint32_t vel, int sounded, uint32_t now)
{
    ui.key_down = (int8_t)k;
    if (guo_shown() && (k == GUO_KEY_BAO || k == GUO_KEY_ROU)) {   /* the 彩蛋: his 包, his 肉包 */
        lg_cmd(LG_SAY, k == GUO_KEY_ROU, 0, 0);
        return;
    }
    if (lg_set.keys_inst != LG_KEYS_LUOGU) {
        lg_cmd(LG_NOTE, (uint32_t)key_midi(k), 0, vel);
        ui.note_shown = (k + 5) % 12;            /* the key's place from C: its 简谱 (F3 is key 0) */
        ui.note_at = now;
        ui.redraw |= 4u;
        return;
    }
    ui.redraw |= 4u;
    if (PKEY[k].inst >= 0) {
        int inst = PKEY[k].inst;
        if (!sounded)
            lg_cmd(LG_HIT, (uint32_t)inst, (uint32_t)PKEY[k].stroke, vel);
        if (lg_set.mine && !lg_kara && (inst == (int)lg_set.mine - 1 || (inst == I_BAN && lg_set.mine == 1u + I_GU)) &&
            PKEY[k].stroke != PS_STOP)
            lg_player_hit(inst, now);
    } else if (pkey_syl[k] >= 0) {           /* a syllable: every stroke of it */
        const lg_syl_t *s = lg_syl(pkey_syl[k]);
        uint32_t i;
        for (i = 0; i < s->n; i++) {
            int inst = s->st[i] >> 4;
            lg_cmd(LG_HIT, (uint32_t)inst, s->st[i] & 15u, s->vel[i] * vel / 127u);
            if (lg_set.mine && !lg_kara && inst == (int)lg_set.mine - 1)
                lg_player_hit(inst, now);
        }
    }
    if (lg_kara && (playing() || key_fits(k))) {   /* 跟练: the engine judges it against the next stroke owed
                                                    * (app_frame takes its verdict); stopped, the first's key starts it */
        uint32_t a, b;
        if (!playing()) {
            ui.practising = 0;
            start();
        }
        kara_key(k, &a, &b);
        lg_cmd(LG_KARA, a, b, 0);
    }
}

static void key_up(int k)
{
    if (lg_set.keys_inst != LG_KEYS_LUOGU)
        lg_cmd(LG_NOTE, (uint32_t)key_midi(k), 0, 0);
    else if (PKEY[k].inst >= 0 && PKEY[k].stroke == PS_ROLL)
        lg_cmd(LG_HIT, (uint32_t)PKEY[k].inst, PS_STOP, 0);   /* a roll ends when the key is let go */
}

/* ------------------------------------------------------------------ input */
static void knob(int k, int32_t e)
{
    ui.turned = k;
    ui.turned_at = plat_ms();
    ui.redraw |= 8u;
    if (ui.page == PG_MIX) {
        int i = k - EN_K1, ch;
        static const uint8_t ROW[3][4] = {{LV_GU, LV_DALUO, LV_NAOBO, LV_XIAOLUO},
                                          {LV_JINGHU, LV_ERHU, LV_YUEQIN, LV_KEYS}, {0, 0, 0, 0}};
        if (ui.mix_row < 2) {
            ch = ROW[ui.mix_row][i];
            set.lg.vol[ch] = (uint8_t)clamp((int32_t)set.lg.vol[ch] + e * 5, 0, 100);
            apply();
            say_value(MIX_NAME[ui.mix_row][i], set.lg.vol[ch], "");
        } else if (i == 0) {
            set.lg.room = (uint8_t)clamp((int32_t)set.lg.room + e * 5, 0, 100);
            apply();
            say_value("混响", set.lg.room, "");
        } else if (i == 1) {
            set.lg.keys_inst = (uint8_t)((set.lg.keys_inst + LG_NKEYS + (e > 0 ? 1 : -1)) % LG_NKEYS);
            apply();
            app_say("键盘", KEYS_NAME[set.lg.keys_inst]);
        } else if (i == 2) {
            set.lg.erhu ^= 1u;
            apply();
            app_say("京二胡", set.lg.erhu ? "开" : "关");
        } else {
            set.lg.yueqin ^= 1u;
            apply();
            app_say("月琴", set.lg.yueqin ? "开" : "关");
        }
        ui.redraw |= 4u;
        return;
    }
    switch (k) {
    case EN_K1: {                            /* 速度 */
        int lo = 30, hi = 320;
        set.lg.bpm = (uint16_t)clamp((int32_t)set.lg.bpm + e * (set.lg.bpm >= 120 ? 4 : 2), lo, hi);
        apply();
        say_value("速度", set.lg.bpm, "");
        break;
    }
    case EN_K2:
        if (ui.page == PG_SING) {            /* 调门, of the 声腔 sung */
            char t[8];
            int q = cur_qiang();
            set.lg.key[q] = (int8_t)((set.lg.key[q] + 12 + (e > 0 ? 1 : -1)) % 12);
            apply();
            key_name(set.lg.key[q], t);
            app_say("调门", t);
        } else {                             /* 乐器 */
            set.lg.mine = (uint8_t)((set.lg.mine + (I_NPERC + 1) + (e > 0 ? 1 : -1)) % (I_NPERC + 1));
            if (set.lg.mine == 1u + I_BAN)   /* (the 板 goes with the 鼓: one player) */
                set.lg.mine = (uint8_t)(e > 0 ? 1u + I_DALUO : 1u + I_GU);
            apply();
            app_say("乐器", MINE_NAME[set.lg.mine]);
        }
        ui.redraw |= 4u;
        break;
    case EN_K3:
        if (ui.page == PG_SING) {               /* the 板 alone, or the 眼 too */
            set.lg.yan ^= 1u;
            apply();
            app_say("板眼", set.lg.yan ? "板和眼" : "只打板");
        } else {
            set.kara ^= 1u;
            apply();
            app_say("跟练", set.kara ? "开: 打对才往下走" : "关");
            ui.redraw |= 7u;
        }
        break;
    case EN_K4:
        set.lg.keys_inst = (uint8_t)((set.lg.keys_inst + LG_NKEYS + (e > 0 ? 1 : -1)) % LG_NKEYS);
        apply();
        app_say("键盘", KEYS_NAME[set.lg.keys_inst]);
        ui.redraw |= 4u;
        break;
    default:
        break;
    }
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), k, now = plat_ms();
    int32_t e;
    touch_poll();
    keys = touch_keys();
    while (midi_key_r != midi_key_w) {       /* MIDI's strokes, as the keys' */
        uint32_t q = midi_key[midi_key_r & 15u];
        midi_key_r++;
        key_down((int)(q & 255u), q >> 8 & 127u, (int)(q >> 16 & 1u), midi_key_at[(midi_key_r - 1u) & 15u]);
    }
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit))
            key_down((int)k, touch_vel(k), 0, now);
        else if (!(keys & bit) && (ui.keys & bit))
            key_up((int)k);
    }
    for (k = 0; k < NB; k++) {               /* a button pressed */
        uint32_t bit = 1u << k;
        if (!(btn & bit) || (ui.btn & bit))
            continue;
        switch (k) {
        case B_PLAY:
            ui.play_at = now ? now : 1u;
            break;
        case B_HOME:                         /* 锣鼓 <-> 唱腔 */
            if (playing())
                lg_cmd(LG_STOP, 0, 0, 0);
            ui.guo = 0;                      /* (the 彩蛋 is the 唱腔 page's: off with it) */
            ui.page = ui.page == PG_SING ? PG_LUOGU : PG_SING;
            set.page = ui.page;
            set.lg.bpm = ui.page == PG_SING ? (lg_nbs() ? sel_bpm() : 80) : lg_pat(set.pat)->bpm;
            apply();
            if (ui.page == PG_SING && sel_tq() >= 0)
                app_say_tq(PAGE_NAME[ui.page], sel_name());
            else
                app_say(PAGE_NAME[ui.page], ui.page == PG_SING ? (lg_nbs() ? sel_name() : "") :
                                                              lg_pat(set.pat)->name);
            ui.redraw |= 15u;
            break;
        case B_GLO:                          /* 混音 and back */
            ui.page = ui.page == PG_MIX ? set.page : PG_MIX;
            app_say(PAGE_NAME[ui.page], "");
            ui.redraw |= 15u;
            break;
        case B_SEQ:                          /* 唱腔: the next 小过门, for the 落音 just played if any */
            if (lg_now.state == LG_SINGING && !has_small(lg_bs(lg_now.bs))) {
                app_say("唱完", "按 PLAY 收");
            } else if (lg_now.state == LG_SINGING) {
                int pc = -1;
                static const char *const JP[12] = {"1", "#1", "2", "#2", "3", "4", "#4", "5", "#5", "6", "b7", "7"};
                if (lg_set.keys_inst != LG_KEYS_LUOGU && ui.note_shown >= 0 && now - ui.note_at < 4000u)
                    pc = ui.note_shown;
                else if (midi_pc >= 0 && now - midi_at < 4000u)
                    pc = midi_pc;
                lg_cmd(LG_CUE, (uint32_t)(pc + 1), 0, 0);
                if (pc >= 0) {
                    char t[12] = "落";
                    text_cat(t, JP[pc]);
                    app_say("过门", t);
                } else {
                    app_say("过门", lg_now.half ? "下句后" : "上句后");
                }
            }
            break;
        case B_ARP:
            set.loop ^= 1u;
            set_dirty = 1;
            app_say("循环", set.loop ? "开" : "关");
            break;
        case B_REC:                          /* practice: from the start, scored */
            if (ui.page == PG_LUOGU) {
                if (!set.lg.mine && !set.kara) {
                    app_say("跟打", "先选乐器 (K2)");
                    break;
                }
                if (playing())
                    lg_cmd(LG_STOP, 0, 0, 0);
                lg_score_reset();
                ui.practising = !set.kara;       /* (karaoke: the tally of right and wrong instead of the timing) */
                ui.judged = 0;
                ui.kara_hits = ui.kara_miss = 0;
                ui.kara_run = set.kara;
                lg_cmd(LG_PLAY_PAT, set.pat, 0, 0);
                app_say(set.kara ? "跟练" : "跟打", MINE_NAME[set.lg.mine]);
            }
            break;
        case B_EDIT:
            ui.show_use ^= 1u;
            ui.redraw |= 4u;
            break;
        case B_LFO:                          /* (nothing else: the 彩蛋's way in) */
            guo_lfo(now);
            break;
        case B_FX:                           /* karaoke, on either page */
            if (ui.page != PG_MIX) {
                set.kara ^= 1u;
                apply();
                app_say("跟练", set.kara ? "开: 打对才往下走" : "关");
                ui.redraw |= 15u;
            }
            break;
        case B_SEL:                          /* the keys' map, and back */
            if (ui.page != PG_MIX) {
                ui.show_keys ^= 1u;
                app_say("键位", ui.show_keys ? "SEL 返回" : "");
                ui.redraw |= 4u;
            }
            break;
        case B_OCTDN:
        case B_OCTUP:
            set.octave = (int8_t)clamp(set.octave + (k == B_OCTUP ? 1 : -1), -1, 1);
            set_dirty = 1;
            lg_cmd(LG_NOTE, 0, 0, 0);
            say_value("八度", set.octave, "");
            break;
        case B_SAVE:
            if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0) {
                set_dirty = 0;
                app_say("已保存", "");
            } else {
                app_say("保存", "失败");
            }
            break;
        default:
            break;
        }
    }
    if (ui.play_at) {                        /* PLAY: on its release; held, stop now */
        if (btn >> B_PLAY & 1u) {
            if (now - ui.play_at >= PLAY_HOLD_MS && playing()) {
                ui.play_at = 0;
                lg_cmd(LG_STOP, 0, 0, 0);
                app_say("停", "");
            }
        } else {
            ui.play_at = 0;
            play_button();
        }
    }
    if ((keys & ~ui.keys) || (btn & ~ui.btn))
        ui.touched = 1;
    for (k = 0; k < NE; k++) {
        if ((e = plat_enc((int)k)) == 0)
            continue;
        ui.touched = 1;
        if (k == EN_SELECT || k == EN_ALGO) {
            if (ui.page == PG_MIX) {
                ui.mix_row = (uint8_t)((ui.mix_row + 3 + (e > 0 ? 1 : -1)) % 3);
                app_say("混音", ui.mix_row == 0 ? "武场" : ui.mix_row == 1 ? "文场" : "其他");
                ui.redraw |= 12u;
            } else if (k == EN_ALGO) {
                choose((int)e);                  /* (a quick turn skips ahead) */
            } else {                             /* SELECT chooses nothing: one knob each, it says which */
                app_say("请用 ALGO", ui.page == PG_LUOGU ? "PRESET 换类" : lg_ntq() ? "PRESET 板式/唱段" : "");
            }
        } else if (k == EN_PRESET) {             /* the group: the kind of pattern; the 板式 or the 唱段 */
            if (ui.page == PG_LUOGU)
                choose_cat(e > 0 ? 1 : -1);
            else if (ui.page == PG_SING)
                choose_group();
        } else {
            knob((int)k, e);
        }
    }
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw |= ui.show_keys && keys != ui.keys ? 12u : 8u;   /* (the map lights the keys held) */
    ui.keys = keys;
    ui.btn = btn;
    plat_glow(1);
    plat_leds(btn | (set_dirty ? 1u << B_SAVE : 0u) | (playing() ? 1u << B_PLAY : 0u) |
              (set.loop && ui.page == PG_LUOGU ? 1u << B_ARP : 0u) | (ui.practising ? 1u << B_REC : 0u), keys);
}

void app_input_only(void) { input(); }

/* ------------------------------------------------------------------ drawing */
#define C_GOLD RGB(255, 190, 60)
#define C_RED RGB(232, 64, 52)
#define C_JADE RGB(80, 210, 150)

static int same_prefix(const char *a, const char *b);
static int px_line(int l, int32_t cx, int32_t y, int sc, int cur, int bright);
static void px_text(const char *s, int32_t cx, int32_t y, int sc, uint16_t c);
static int32_t px_text_at(const char *s, int32_t x, int32_t y, int sc, uint16_t c);

static void draw_header(void)
{
    char t[32];
    uint32_t now = plat_ms();
    cv_begin(240, 32, C_BLACK);
    if (now < ui.msg_until) {
        int32_t x = zh_text(8, 7, &ZM, ui.msg[0], C_WHITE);
        if (ui.msg_px)
            px_text_at(ui.msg[1], x + 8, 10, 1, C_GOLD);
        else
            zh_text(x + 8, 7, &ZM, ui.msg[1], C_GOLD);
    } else {
        int32_t x = zh_text(8, 7, &ZM, guo_shown() ? "郭德纲" : PAGE_NAME[ui.page], C_WHITE);
        if (ui.page != PG_MIX) {
            const char *st = !playing() ? "停" : lg_now.seg == SEG_GM ? (lg_now.tq >= 0 && lg_now.sing ? "唱" : "过门") :
                             lg_now.seg == SEG_KEEP ? "唱" :
                             lg_now.sec == LG_QI ? "起" : lg_now.sec == LG_ZHONG ? "中" : "收";
            x = zh_text(x + 10, 7, &ZM, st, playing() ? C_GOLD : C_GRAY);
            if (playing() && lg_now.cut)
                zh_text(x + 6, 9, &ZS, "将收", C_RED);
            if (playing() && lg_now.cue)
                zh_text(x + 6, 9, &ZS, "过门将起", C_RED);
        }
        t[0] = 0;
        text_cat(t, "速 ");
        int_text(lg_set.bpm, t + len_of(t));
        zh_text_r(232, 7, &ZM, t, C_GRAY);
        if (lg_kara && ui.page != PG_MIX)    /* karaoke on: said in the corner */
            zh_text_r(232 - zh_w(&ZM, t) - 8, 9, &ZS, "跟练", C_JADE);
    }
    cv_rect(0, 31, 240, 1, C_LINE);
    cv_blit(0, 0);
}

/* the title strip: the pattern's (or the 板式's) name, its kind and meter */
static void draw_title(void)
{
    char t[40];
    cv_begin(240, 72, C_BLACK);
    if (guo_shown()) {                       /* the 彩蛋: the picture's top 36 rows */
        guo_draw(0, 36);
    } else if (ui.page == PG_LUOGU) {
        const lg_pat_t *p = lg_pat(set.pat);
        t[0] = 0;
        text_cat(t, lg_cat_name(p->cat));
        text_cat(t, " · ");
        text_cat(t, p->mixed ? "变换拍子" : METER_NAME[p->meter <= 4 ? p->meter : 0]);
        zh_text(8, 2, &ZS, t, C_GRAY);
        int_text(set.pat + 1, t);
        text_cat(t, "/");
        int_text(lg_npat(), t + len_of(t));
        zh_text_r(232, 2, &ZS, t, C_DIM);
        t[0] = 0;
        text_cat(t, T_("【"));
        text_cat(t, p->name);
        text_cat(t, T_("】"));
        zh_text_c(120, 20, &ZT, t, C_GOLD);
        zh_text_c(120, 54, &ZS, p->py, C_GRAY);
    } else if (ui.page == PG_SING && sel_tq() >= 0) {   /* a 唱段: its play, its 板式, who sings it, its first line */
        const lg_tq_t *a = lg_tq(sel_tq());
        const char *last = lg_tq_sec(a->s0 + a->ns - 1)->bs;
        key_name(lg_set.key[a->qiang], t);
        text_cat(t, " · ");
        text_cat(t, lg_tq_sec(a->s0)->bs);
        if (a->ns > 1) {
            text_cat(t, "→");
            text_cat(t, last + (same_prefix(last, lg_tq_sec(a->s0)->bs) ? 6 : 0));   /* (the 声腔 said once) */
        }
        zh_text(8, 2, &ZS, t, C_GRAY);
        zh_text_r(232, 2, &ZS, a->role, C_DIM);
        px_text(a->play, 120, 24, 2, C_GOLD);   /* (the pixel font, as its lyrics: its own glyphs) */
        px_line(a->l0, 120, 56, 1, -1, 0);
    } else if (ui.page == PG_SING) {
        if (lg_nbs()) {
            const lg_bs_t *b = lg_bs(sel_bs());
            key_name(lg_set.key[b->qiang], t);
            text_cat(t, " · ");
            text_cat(t, METER_NAME[b->meter <= 4 ? b->meter : 0]);
            zh_text(8, 2, &ZS, t, C_GRAY);
            t[0] = 0;
            text_cat(t, T_("【"));
            text_cat(t, b->name);
            text_cat(t, T_("】"));
            zh_text_c(120, 20, &ZT, t, C_GOLD);
        }
    } else {
        zh_text_c(120, 20, &ZT, ui.mix_row == 0 ? T_("武场") : ui.mix_row == 1 ? T_("文场") : T_("其他"), C_GOLD);
    }
    cv_blit(0, 32);
}

/* a section's syllables, from x, y (wrapping at the right edge, rows 34 apart): the one sounding lit, its row's y in
 * *lit_y; draw 0 only lays them out */
static void syl_run(const lg_sec_t *s, int32_t *x, int32_t *y, int live, int lit, int want, int draw,
                    int32_t *lit_y)
{
    uint32_t i;
    for (i = 0; i < s->nev; i++) {
        const lg_ev_t *e = &s->ev[i];
        const char *zi = lg_syl(e->syl)->zi;
        int32_t w = zh_w(&ZL, zi) + 4;
        uint16_t c = live ? ((int)i < lit ? C_DIM : (int)i == lit ? C_BLACK : C_WHITE) : C_AMB;
        int waiting = lg_kara && (int)i == want;   /* 跟练: the stroke struck next */
        if (e->fl & LGF_FILL)                /* (the 鼓师's own strokes: not in the 字谱) */
            continue;
        if (lg_kara)                         /* 跟练: dark till the player strikes it, then yellow (a rest, 乙: dark) */
            c = live && (int)i <= lit && lg_syl(e->syl)->n ? C_GOLD : waiting ? C_WHITE : C_DIM;
        if ((e->fl & LGF_BAR) && i) {         /* a bar line */
            if (draw && *y >= 0)
                cv_rect(*x, *y + 4, 1, 22, C_LINE);
            *x += 4;
        }
        if (*x + w > 236) {
            *x = 6;
            *y += 34;
        }
        if (live && (int)i == lit)
            *lit_y = *y;
        if (draw && *y >= 0) {
            if (waiting) {                   /* the stroke the time waits for: a box round it */
                cv_rect(*x - 1, *y, w, 1, C_WHITE);
                cv_rect(*x - 1, *y + 27, w, 1, C_WHITE);
                cv_rect(*x - 1, *y, 1, 28, C_WHITE);
                cv_rect(*x + w - 2, *y, 1, 28, C_WHITE);
            } else if (live && (int)i == lit && !lg_kara)
                cv_rect(*x - 1, *y, w, 28, C_GOLD);
            zh_text(*x + 1, *y, &ZL, zi, c);
            if (e->fl & LGF_BEAT)            /* 板 (x) on the bar's first beat, 眼 (o) on the others */
                zh_text(*x + w / 2 - 3, *y + 26, &ZS, (e->fl & LGF_BAR) ? "×" : "○", C_GRAY);
        }
        *x += w;
    }
}

/* a pattern's 字谱 from y: 起, 中 (with the times round), 收; returns its last row's y, the sounding stroke's in
 * *lit_y */
static int32_t luogu_runs(const lg_pat_t *p, int live, int32_t y, int draw, int32_t *lit_y)
{
    char t[8];
    int32_t x = 6;
    int sec, ksec = -1, kev = -1;
    if (lg_kara && live && lg_now.seg == SEG_PAT) {   /* 跟练: the stroke struck next (stopped: the first), boxed */
        ksec = lg_now.sec;
        kev = lg_now.want;
    } else if (lg_kara && !playing()) {
        kev = kara_first(p, &ksec);
    }
    for (sec = 0; sec < 3; sec++) {
        int on = live && lg_now.sec == sec;
        if (!p->s[sec].nev)
            continue;
        if (x > 6)
            x += 6;
        if (x + 16 + zh_w(&ZL, lg_syl(p->s[sec].ev[0].syl)->zi) + 4 > 236) {   /* (the label with its first) */
            x = 6;
            y += 34;
        }
        if (draw && y >= 0)
            zh_text(x, y + 6, &ZS, sec == LG_QI ? "起" : sec == LG_ZHONG ? "中" : "收", on ? C_RED : C_DIM);
        x += 16;
        syl_run(&p->s[sec], &x, &y, on, lg_now.ev, sec == ksec ? kev : -1, draw, lit_y);
        if (sec == LG_ZHONG && live && lg_now.rep) {
            t[0] = 0;
            text_cat(t, "×");
            int_text(lg_now.rep + 1, t + len_of(t));
            if (draw && y >= 0)
                zh_text(x + 2, y + 6, &ZS, t, C_GRAY);
            x += 22;
        }
    }
    return y;
}

/* ------------------------------------------------------------------ the 唱腔 page */
/* a 过门's 简谱: the digit (0 a rest), a sharp or flat small at its upper left, the octave's dots (above, or below
 * the lines), the length's lines under it (one an eighth, two a sixteenth, three a thirty-second; joined within a
 * beat), a dot after a dotted note, a dash for each beat more; a grace note small before it; the bar lines */
static const char *const JP_DIGIT[12] = {"1", "1", "2", "2", "3", "4", "4", "5", "5", "6", "7", "7"};
static const char *const JP_ACC[12] = {0, "#", 0, "#", 0, 0, "#", 0, "#", 0, "b", 0};

static int jp_lines(uint32_t len) { return len >= LG_TPB ? 0 : len >= LG_TPB / 2 ? 1 : len >= LG_TPB / 4 ? 2 : 3; }
static int jp_dotted(uint32_t len)
{
    return len == LG_TPB * 3 / 2 || len == LG_TPB * 3 / 4 || len == LG_TPB * 3 / 8 || len == LG_TPB * 3 / 16;
}
static int jp_dashes(uint32_t len) { return len >= 2 * LG_TPB ? (int)(len / LG_TPB) - 1 : 0; }
static int jp_pc(int p) { return ((p % 12) + 12) % 12; }
static int jp_octave(int p) { return p >= 0 ? p / 12 : -((11 - p) / 12); }

static int32_t jp_head(const lg_note_t *n)     /* the width before the digit: its grace note, its sharp */
{
    return ((n->fl & LGN_GRACE) ? 7 : 0) + (!(n->fl & LGN_REST) && JP_ACC[jp_pc(n->p)] ? 6 : 0);
}

static int jp_crosses(const lg_note_t *n, uint32_t bar, int k)   /* its k-th dash starts a bar */
{
    return bar && (n->at + (uint32_t)(k + 1) * LG_TPB) % bar == 0;
}

static int32_t jp_w(const lg_note_t *n, uint32_t bar)   /* the note's whole width: its dashes, bar lines among them */
{
    int nl = jp_lines(n->len), k, nd = jp_dashes(n->len);
    int32_t w = jp_head(n) + (nl >= 2 ? 10 : nl == 1 ? 12 : 14) + (jp_dotted(n->len) ? 5 : 0) + 12 * nd;
    for (k = 0; k < nd; k++)
        w += jp_crosses(n, bar, k) ? 6 : 0;
    return w;
}

static int32_t jp_gap(const lg_gm_t *g, uint32_t i, uint32_t i0)   /* before note i: a bar line, or a beat's gap */
{
    uint32_t bar = (uint32_t)g->meter * LG_TPB;
    if (i == i0)
        return 0;
    return bar && g->n[i].at % bar == 0 ? 6 : g->n[i].at % LG_TPB == 0 ? 3 : 0;
}

/* from the bar the note cur is in (or its beat, when the bar is too long to show it; 散: from the note before);
 * lit: the note sounding, the ones played dimmed */
/* ------------------------------------------------------------------ a 唱段: its lyrics */
static int same_prefix(const char *a, const char *b)   /* the same first two characters (a 声腔: 西皮, 二黄) */
{
    int i;
    for (i = 0; i < 6; i++)
        if (!a[i] || a[i] != b[i])
            return 0;
    return 1;
}

static void px_char(int32_t x, int32_t y, int g, int sc, uint16_t c)   /* a lyric's character (12 x 12), sc x */
{
    const uint8_t *b = lg_tq_glyph(g);
    int i;
    for (i = 0; i < 144; i++)
        if (b[i >> 3] & (0x80u >> (i & 7)))
            cv_rect(x + (i % 12) * sc, y + (i / 12) * sc, sc, sc, c);
}

static int px_codes(const char *s, uint32_t *code)   /* UTF-8's code points (at most 16) */
{
    int n = 0;
    while (*s && n < 16) {
        uint32_t b = (uint8_t)*s++;
        if (b >= 0xE0u && s[0] && s[1]) {
            b = (b & 15u) << 12 | ((uint32_t)s[0] & 63u) << 6 | ((uint32_t)s[1] & 63u);
            s += 2;
        }
        code[n++] = b;
    }
    return n;
}

static int32_t px_text_at(const char *s, int32_t x, int32_t y, int sc, uint16_t c)   /* from x; returns its end */
{
    uint32_t code[16];
    int n = px_codes(s, code), i;
    for (i = 0; i < n; i++) {
        int g = lg_tq_glyph_of(code[i]);
        if (g >= 0)
            px_char(x + i * 12 * sc, y, g, sc, c);
    }
    return x + n * 12 * sc;
}

static void px_text(const char *s, int32_t cx, int32_t y, int sc, uint16_t c)   /* UTF-8, centred on cx */
{
    uint32_t code[16];
    px_text_at(s, cx - px_codes(s, code) * 6 * sc, y, sc, c);
}

/* line l's characters from y, centred on cx (cx < 0: from x 6), sc x, wrapping: those up to syllable cur yellow
 * (cur < 0: none), the rest dim (bright: white); an interjection dimmer. Returns the rows it took */
static int px_line(int l, int32_t cx, int32_t y, int sc, int cur, int bright)
{
    const lg_tq_line_t *ln = lg_tq_line(l);
    int per = 228 / (12 * sc), rows = (ln->n + per - 1) / per, i;
    for (i = 0; i < ln->n; i++) {
        const lg_tq_syl_t *y0 = lg_tq_syl(ln->y0 + i);
        int row = i / per, col = i % per, in_row = row < rows - 1 ? per : ln->n - row * per;
        int32_t x = cx < 0 ? 6 + col * 12 * sc : cx - in_row * 6 * sc + col * 12 * sc;
        uint16_t c = ln->y0 + i <= cur ? C_GOLD : bright ? C_WHITE : C_DIM;
        if (y0->dim && c != C_GOLD)
            c = C_LINE;
        px_char(x, y + row * 13 * sc, y0->glyph, sc, c);
    }
    return rows;
}

static int tq_syl_now(const lg_tq_t *a)  /* the syllable sung now (its last begun), -1 none yet */
{
    const lg_tq_line_t *last = lg_tq_line(a->l0 + a->nl - 1);
    int i, y0 = lg_tq_line(a->l0)->y0, y1 = last->y0 + last->n, cur = -1;
    for (i = y0; i < y1 && lg_tq_syl(i)->at <= lg_now.tq_at; i++)
        cur = i;
    return cur;
}

static void draw_tq(int t)               /* a 唱段: the line sung now big (sung yellow), the next, the 板眼, where */
{
    const lg_tq_t *a = lg_tq(t);
    int live = playing() && lg_now.tq == t && lg_now.seg == SEG_GM;
    int cur = live ? tq_syl_now(a) : -1, l = 0, i, m = live && lg_now.meter ? lg_now.meter : 1;
    uint32_t end = lg_tq_sec(a->s0 + a->ns - 1)->at + lg_tq_sec(a->s0 + a->ns - 1)->len;
    char t2[24];
    for (i = 0; i < a->nl; i++)              /* the line of the syllable sung, or after its last the next */
        if (cur >= (int)lg_tq_line(a->l0 + i)->y0)
            l = i;
    if (cur >= 0 && cur == (int)(lg_tq_line(a->l0 + l)->y0 + lg_tq_line(a->l0 + l)->n - 1) && l + 1 < a->nl &&
        lg_now.tq_at > lg_tq_syl(cur)->at + LG_TPB)
        l++;
    /* the top: which line (上句, 下句), 开唱 at the first word; the 板眼, small, at the right */
    t2[0] = 0;
    if (!live) {
        text_cat(t2, playing() ? "" : "PLAY 开始 · PRESET 板式");
    } else if (cur < 0) {
        text_cat(t2, "过门");
    } else {
        text_cat(t2, "第");
        int_text(l + 1, t2 + len_of(t2));
        text_cat(t2, lg_tq_line(a->l0 + l)->typ ? "句 · 下句" : "句 · 上句");
    }
    if (live && cur == (int)lg_tq_line(a->l0)->y0)
        zh_text(6, 0, &ZM, "开唱", C_RED);
    else
        zh_text(6, 2, &ZS, t2, live ? C_JADE : C_GRAY);
    if (live)
        for (i = 0; i < m && i < 4; i++) {
            int lit = (lg_now.beat % m) == i;
            cv_rect(232 - (m - i) * 16, 4, 13, 11, lit ? (i == 0 ? C_RED : C_GOLD) : C_LINE);
        }
    /* the line, big; the next, small */
    {
        int rows = px_line(a->l0 + l, 120, 22, 2, cur, live);
        if (l + 1 < a->nl && rows < 3)
            px_line(a->l0 + l + 1, 120, 22 + rows * 26 + 4, 1, -1, 0);
    }
    /* where it is, from its start to its end */
    cv_rect(6, 98, 228, 3, C_LINE);
    if (live && end)
        cv_rect(6, 98, (int32_t)(228u * lg_now.tq_at / end), 3, C_AMB);
}

static void draw_jianpu(const lg_gm_t *g, int cur, int lit, int32_t y)
{
    uint32_t bar = (uint32_t)g->meter * LG_TPB, i, i0;
    int32_t x = 6, xc = 0;
    if (!g->nn)
        return;
    cur = cur < 0 ? 0 : cur >= (int)g->nn ? (int)g->nn - 1 : cur;
    i0 = (uint32_t)cur;
    if (bar) {
        uint32_t b0 = g->n[cur].at / bar * bar;
        while (i0 > 0 && g->n[i0 - 1].at >= b0)
            i0--;
        for (i = i0; i < (uint32_t)cur; i++)
            xc += jp_gap(g, i, i0) + jp_w(&g->n[i], bar);
        if (xc > 150) {                       /* past the middle: from its beat */
            uint32_t q0 = g->n[cur].at / LG_TPB * LG_TPB;
            i0 = (uint32_t)cur;
            while (i0 > 0 && g->n[i0 - 1].at >= q0)
                i0--;
        }
    } else if (i0 > 0)
        i0--;
    for (i = i0; i < g->nn; i++) {
        const lg_note_t *n = &g->n[i];
        int pc = jp_pc(n->p), oc = jp_octave(n->p), nl = jp_lines(n->len), nd = jp_dashes(n->len), k;
        int on = lit && (int)i == cur;
        uint16_t c = on ? C_BLACK : lit && (int)i < cur ? C_DIM : C_WHITE, cl = lit && (int)i < cur ? C_DIM : C_AMB;
        int32_t gap = jp_gap(g, i, i0), dx;
        int32_t wd = jp_head(n) + (nl >= 2 ? 10 : nl == 1 ? 12 : 14) + (jp_dotted(n->len) ? 5 : 0);   /* (no dashes) */
        if (x + gap + wd > 238)
            break;
        if (gap == 6)
            cv_rect(x + 2, y + 5, 1, 24, C_GRAY);
        x += gap;
        dx = x + jp_head(n);
        if (n->fl & LGN_GRACE) {
            zh_text(x, y - 5, &ZS, JP_DIGIT[jp_pc(n->g)], lit && (int)i < cur ? C_DIM : C_GRAY);
            if (jp_octave(n->g) > 0)
                cv_rect(x + 3, y, 2, 2, C_GRAY);
        }
        if (!(n->fl & LGN_REST) && JP_ACC[pc])
            zh_text(dx - 6, y - 2, &ZS, JP_ACC[pc], c == C_BLACK ? C_GOLD : c);
        if (on)
            cv_rect(dx - 2, y + 5, 12, 16, C_GOLD);
        zh_text(dx, y, &ZM, (n->fl & LGN_REST) ? "0" : JP_DIGIT[pc], c);
        for (k = 0; k < oc && k < 2; k++)     /* the octave's dots */
            cv_rect(dx + 3, y + 3 - 3 * k, 2, 2, on ? C_GOLD : c);
        for (k = 0; k < -oc && k < 2; k++)
            cv_rect(dx + 3, y + 22 + 3 * nl + 3 * k, 2, 2, on ? C_GOLD : c);
        for (k = 0; k < nl; k++) {            /* the length's lines: on to the next note in the beat */
            int join = i + 1 < g->nn && g->n[i + 1].at % LG_TPB != 0 && jp_lines(g->n[i + 1].len) > k;
            cv_rect(x, y + 21 + 3 * k, wd - (join ? 0 : 2), 1, cl);
        }
        if (jp_dotted(n->len))
            cv_rect(dx + 10, y + 12, 2, 2, c == C_BLACK ? C_GOLD : c);
        x += wd;
        for (k = 0; k < nd && x + 12 <= 238; k++) {   /* the dashes, a bar line before one that starts a bar */
            if (jp_crosses(n, bar, k)) {
                cv_rect(x + 2, y + 5, 1, 24, C_GRAY);
                x += 6;
            }
            cv_rect(x + 2, y + 12, 7, 2, cl);
            x += 12;
        }
    }
}

/* a pattern's section in small characters on one line, the stroke sounding lit (the lead-in on the 唱腔 page) */
static int32_t syl_step(const lg_sec_t *s, uint32_t i)   /* the room event i takes on the line (a fill none) */
{
    if (s->ev[i].fl & LGF_FILL)
        return 0;
    return 18 + ((s->ev[i + 1 < s->nev ? i + 1 : i].fl & LGF_BAR) ? 5 : 0);
}

static void draw_syl_line(const lg_sec_t *s, int lit, int want, int32_t y)
{
    uint32_t i, i0 = 0;
    int32_t x = 6, xl = 0;
    for (i = 0; i < s->nev && (int)i < lit; i++)
        xl += syl_step(s, i);
    while (xl > 150 && i0 < s->nev) {         /* the stroke past the middle: drop strokes off the left */
        xl -= syl_step(s, i0);
        i0++;
    }
    for (i = i0; i < s->nev && x < 226; i++) {
        const lg_ev_t *e = &s->ev[i];
        int on = (int)i == lit, waiting = lg_kara && (int)i == want;
        uint16_t c = on ? C_BLACK : (int)i < lit ? C_DIM : C_WHITE;
        if (e->fl & LGF_FILL)
            continue;
        if ((e->fl & LGF_BAR) && i > i0) {
            cv_rect(x, y + 6, 1, 16, C_LINE);
            x += 5;
        }
        if (lg_kara) {                       /* 跟练: dark till struck, then yellow; the one struck next black on
                                              * white (a box's lines would cross the small syllable: 大 as 天) */
            c = (int)i <= lit && lg_syl(e->syl)->n ? C_GOLD : waiting ? C_BLACK : C_DIM;   /* (乙: dark) */
            if (waiting)
                cv_rect(x - 1, y + 4, 18, 20, C_WHITE);
        } else if (on) {
            cv_rect(x - 1, y + 4, 18, 20, C_GOLD);
        }
        zh_text(x, y, &ZM, lg_syl(e->syl)->zi, c);
        x += 18;
    }
}

static void draw_sing(void)
{
    const lg_bs_t *b = lg_bs(sel_bs());
    if (lg_now.seg != SEG_GM)
        ui.jp_gm = -1;
    int32_t m = playing() && lg_now.meter ? lg_now.meter : b->meter, i, beat = lg_now.beat;
    static const char *const EYE4[4] = {"板", "头眼", "中眼", "末眼"};
    char t[32];
    if (playing() ? lg_now.tq >= 0 && lg_now.seg == SEG_GM : sel_tq() >= 0 && !lg_kara) {
        draw_tq(playing() ? lg_now.tq : sel_tq());   /* a 唱段 (its lead-in, and 跟练's first stroke, as a 板式's) */
        return;
    }
    /* the line at the top: what plays, or (stopped) what the 板式 is for, on up to three lines */
    if (!playing())
        zh_wrap(6, 0, &ZS, b->use[0] ? b->use : b->name, C_GRAY, 228, 18, 3);
    else if (lg_now.seg == SEG_GM && lg_now.sing)
        zh_text_c(120, 0, &ZM, "开唱", C_RED);
    else if (lg_now.seg == SEG_KEEP)
        zh_text_c(120, 0, &ZM, lg_now.half ? "唱下句" : "唱上句", C_JADE);
    else
        zh_text_c(120, 0, &ZM, lg_now.seg == SEG_GM ? lg_gm(lg_now.gm)->name : lg_pat(lg_now.pat)->name, C_WHITE);
    /* the 板眼, the beat lit (散板: none) */
    if (!playing())
        ;
    else if (m >= 1)
        for (i = 0; i < m && i < 4; i++) {
            int lit = playing() && (beat % m) == i;
            int32_t bw = m == 4 ? 54 : 52, bx = 120 - m * bw / 2 + i * bw;
            const char *nm = m == 4 ? EYE4[i] : i == 0 ? "板" : "眼";
            cv_rect(bx + 2, 26, bw - 4, 26, lit ? (i == 0 ? C_RED : C_GOLD) : C_LINE);
            zh_text_c(bx + bw / 2, 27, &ZM, nm, lit ? C_BLACK : C_DIM);
        }
    else
        zh_text_c(120, 27, &ZM, "散板 · 自由节奏", C_DIM);
    /* below: the 过门's 简谱, the lead-in's 字谱, or while singing what to do */
    if (!playing()) {
        const lg_pat_t *ip = lg_kara ? kara_run_pat() : 0;
        int ks = -1, ke = ip ? kara_first(ip, &ks) : -1;
        if (ke >= 0) {                        /* 跟练: the lead-in, its first stroke boxed (its key starts it) */
            draw_syl_line(&ip->s[ks], -1, ke, 62);
            kara_want(t, "打 ");
            text_cat(t, " 开始");
            zh_text(6, 86, &ZS, t, C_WHITE);
            zh_text_r(234, 86, &ZS, "FX 关跟练", C_GRAY);   /* (this page's footer has no 跟练: how to leave it) */
        } else if (b->big >= 0) {
            draw_jianpu(lg_gm(b->big), 0, 0, 62);
        }
    } else if (lg_now.seg == SEG_GM) {
        if (ui.jp_gm != lg_now.gm) {
            ui.jp_gm = lg_now.gm;
            ui.jp_note = 0;
        }
        if (lg_now.note >= 0)
            ui.jp_note = lg_now.note;
        draw_jianpu(lg_gm(lg_now.gm), ui.jp_note, 1, 62);
    } else if (lg_now.seg == SEG_PAT) {
        draw_syl_line(&lg_pat(lg_now.pat)->s[lg_now.sec], lg_now.ev, lg_now.want, 62);
        if (lg_kara && lg_now.want >= 0) {    /* 跟练: what the player strikes next */
            kara_want(t, "该打 ");
            zh_text(6, 86, &ZS, t, C_WHITE);
        } else if (lg_kara) {
            zh_text(6, 86, &ZS, "打对了，往下走", C_JADE);
        }
        if (lg_kara)
            zh_text_r(234, 86, &ZS, "FX 关跟练", C_GRAY);
    } else {
        int sm = has_small(lg_now.bs >= 0 ? lg_bs(lg_now.bs) : b);
        if (sm)
            zh_text(6, 62, &ZS, lg_now.cue ? "过门将起" : "句末 SEQ: 小过门", lg_now.cue ? C_RED : C_GRAY);
        zh_text(6, 82, &ZS, lg_now.cut ? "将收" : sm ? "PLAY: 收" : "唱完 PLAY: 收", lg_now.cut ? C_RED : C_GRAY);
        if (lg_now.cue && lg_now.cue_pc >= 0) {
            t[0] = 0;
            text_cat(t, "落 ");
            text_cat(t, JP_DIGIT[lg_now.cue_pc % 12]);
            zh_text(70, 62, &ZS, t, C_GOLD);
        }
    }
    if (lg_set.keys_inst != LG_KEYS_LUOGU && ui.note_shown >= 0 && lg_now.seg != SEG_GM) {   /* the key's 简谱 */
        t[0] = 0;
        text_cat(t, KEYS_NAME[lg_set.keys_inst]);
        text_cat(t, " ");
        if (JP_ACC[ui.note_shown % 12])
            text_cat(t, JP_ACC[ui.note_shown % 12]);
        text_cat(t, JP_DIGIT[ui.note_shown % 12]);
        zh_text_r(234, 62, &ZM, t, C_GOLD);
    }
}

/* ------------------------------------------------------------------ the keys' map (SEL) */
static const uint8_t KEY_WHITE[16] = PLAT_WHITE_KEYS, KEY_BLACK[11] = PLAT_BLACK_KEYS;

static uint16_t key_colour(int k)        /* whose stroke the key is: the 鼓师's gold, 大锣 red, 铙钹 jade, 小锣 white */
{
    if (PKEY[k].inst < 0)
        return C_AMB;                    /* a whole syllable */
    switch (PKEY[k].inst) {
    case I_GU:
    case I_BAN:
        return C_GOLD;
    case I_DALUO:
        return C_RED;
    case I_NAOBO:
        return C_JADE;
    case I_XIAOLUO:
        return C_WHITE;
    default:
        return C_GRAY;                   /* the 堂鼓 */
    }
}

static int key_label(int k, char *t)     /* what key k plays now (its stroke, or its 简谱); returns its octave's dots */
{
    int pc, oct;
    t[0] = 0;
    if (lg_set.keys_inst == LG_KEYS_LUOGU) {
        text_cat(t, PKEY_NAME[k]);
        return 0;
    }
    pc = (k + 5) % 12;                   /* (F3 is key 0; C4, key 7, the 京胡's middle 1) */
    oct = (k + 5) / 12 - 1 + set.octave;
    if (JP_ACC[pc])
        text_cat(t, JP_ACC[pc]);
    text_cat(t, JP_DIGIT[pc]);
    return oct;
}

static void key_dots(int32_t x, int32_t y, int oct, uint16_t c)   /* an octave's dots under (or over) a digit */
{
    int i;
    for (i = 0; i < (oct < 0 ? -oct : oct) && i < 2; i++)
        cv_rect(x - 1, oct < 0 ? y + 18 + 3 * i : y + 4 - 3 * i, 2, 2, c);
}

static void draw_keymap(void)
{
    static const char *const GROUP[5] = {"鼓师", "大锣", "铙钹", "小锣", "整字"};
    static const uint8_t GROUP_X[5] = {30, 75, 105, 135, 202};   /* under their white keys */
    char t[8];
    int i, oct;
    zh_text(6, 0, &ZS, lg_set.keys_inst == LG_KEYS_LUOGU ? "每样乐器：左手→右手  乙=空拍不打" : "白键从 C 起：简谱 1 至 7",
            C_GRAY);                         /* (乙, a rest: no key) */
    for (i = 0; i < 16; i++) {           /* the white keys, their names inside */
        int k = KEY_WHITE[i], held = (int)(ui.keys >> k & 1u);
        int32_t x = i * 15;
        uint16_t c = key_colour(k);
        cv_rect(x, 40, 14, 46, held ? c : C_LINE);
        oct = key_label(k, t);
        zh_text_c(x + 7, 62, &ZS, t, held ? C_BLACK : c);
        key_dots(x + 7, 62, oct, held ? C_BLACK : c);
    }
    for (i = 0; i < 11; i++) {           /* the black keys between, their names over them */
        int k = KEY_BLACK[i], held = (int)(ui.keys >> k & 1u), wi = 0;
        uint16_t c = key_colour(k);
        int32_t x;
        while (wi < 15 && KEY_WHITE[wi] != k - 1)
            wi++;
        x = (wi + 1) * 15 - 5;
        cv_rect(x - 1, 40, 11, 25, C_BLACK);
        cv_rect(x, 40, 9, 24, held ? c : C_DIM);
        oct = key_label(k, t);
        zh_text_c(x + 4, 19, &ZS, t, held ? C_WHITE : c);
        key_dots(x + 4, 19, oct, c);
    }
    if (lg_set.keys_inst == LG_KEYS_LUOGU)
        for (i = 0; i < 5; i++)
            zh_text_c(GROUP_X[i], 86, &ZS, GROUP[i], i == 4 ? C_AMB : C_GRAY);
}

static void draw_body(void)
{
    char t[48];
    cv_begin(240, 104, C_BLACK);
    if (ui.show_keys && ui.page != PG_MIX) {
        draw_keymap();
        cv_blit(0, 104);
        return;
    }
    if (guo_shown()) {                       /* the 彩蛋: the picture's other 52 rows */
        guo_draw(36, 52);
        cv_blit(0, 104);
        return;
    }
    if (ui.page == PG_LUOGU) {
        const lg_pat_t *p = lg_pat(set.pat);
        int live = lg_now.state == LG_PATTERN && lg_now.pat == (int8_t)set.pat;
        if (ui.show_use) {
            zh_text(6, 4, &ZS, "用途", C_GRAY);
            zh_text(6, 22, &ZM, p->use[0] ? p->use : "-", C_WHITE);
        } else {                              /* more rows than fit: the sounding row second from the top */
            int32_t lit_y = -1, last = luogu_runs(p, live, 2, 0, &lit_y), off = 0;
            if (last > 70 && lit_y > 36)
                off = lit_y - 36 < last - 70 ? lit_y - 36 : last - 70;
            luogu_runs(p, live, 2 - off, 1, &lit_y);
            if (last > 70 && off < last - 70)   /* more below */
                zh_text_r(236, 86, &ZS, "▼", C_DIM);
        }
        if (lg_set.keys_inst == LG_KEYS_LUOGU && ui.key_down >= 0 && (ui.keys >> ui.key_down & 1u)) {
            static const char *const INAME[I_NPERC] = {"鼓", "板", "大锣", "铙钹", "小锣", "堂鼓"};
            t[0] = 0;                         /* the key held: its stroke, and whose */
            text_cat(t, PKEY_NAME[ui.key_down]);
            text_cat(t, " · ");
            text_cat(t, PKEY[ui.key_down].inst >= 0 ? INAME[PKEY[ui.key_down].inst] : "全体");
            zh_text_r(234, 84, &ZS, t, C_GOLD);
        }
        if (lg_kara && (live ? lg_now.want >= 0 : !playing())) {   /* 跟练: what the player strikes next
                                                                    * (stopped: the first, its key starts it) */
            kara_want(t, live ? "该打 " : "打 ");
            if (!live && t[0])
                text_cat(t, " 开始");
            zh_text(6, 84, &ZS, t, C_WHITE);
        } else if (lg_set.mine) {             /* practice: the player's instrument and how it goes */
            t[0] = 0;
            text_cat(t, "乐器 ");
            text_cat(t, MINE_NAME[lg_set.mine]);
            zh_text(6, 84, &ZS, t, C_JADE);
            if (lg_score.due) {
                int pct = (int)((lg_score.good * 100u + lg_score.early * 50u + lg_score.late * 50u) /
                                (lg_score.due + lg_score.extra));
                t[0] = 0;
                text_cat(t, "准");
                int_text(lg_score.good, t + len_of(t));
                text_cat(t, " 早");
                int_text(lg_score.early, t + len_of(t));
                text_cat(t, " 晚");
                int_text(lg_score.late, t + len_of(t));
                text_cat(t, " 漏");
                int_text(lg_score.miss, t + len_of(t));
                text_cat(t, " ");
                int_text(pct, t + len_of(t));
                text_cat(t, "%");
                zh_text_r(234, 84, &ZS, t, C_WHITE);
            }
        }
    } else if (ui.page == PG_SING) {
        if (lg_nbs())
            draw_sing();
    } else {
        int i;
        static const uint8_t ROW[2][4] = {{LV_GU, LV_DALUO, LV_NAOBO, LV_XIAOLUO},
                                          {LV_JINGHU, LV_ERHU, LV_YUEQIN, LV_KEYS}};
        for (i = 0; i < 4; i++) {
            int32_t x = 6 + i * 58, v;
            const char *val = 0;
            zh_text(x, 4, &ZS, MIX_NAME[ui.mix_row][i], C_GRAY);
            if (ui.mix_row < 2) {
                v = lg_set.vol[ROW[ui.mix_row][i]];
                cv_rect(x, 26 + (100 - v) * 60 / 100, 20, v * 60 / 100, C_GOLD);
                int_text(v, t);
                zh_text(x + 24, 70, &ZS, t, C_WHITE);
            } else {
                if (i == 0) {
                    int_text(lg_set.room, t);
                    val = t;
                } else if (i == 1)
                    val = KEYS_NAME[lg_set.keys_inst];
                else if (i == 2)
                    val = lg_set.erhu ? "开" : "关";
                else
                    val = lg_set.yueqin ? "开" : "关";
                zh_text(x, 40, &ZM, val, C_WHITE);
            }
        }
    }
    cv_blit(0, 104);
}

static void draw_footer(void)
{
    char t[24];
    int i;
    cv_begin(240, 32, C_BLACK);
    cv_rect(0, 0, 240, 1, C_LINE);
    if (guo_shown())
        zh_text_c(120, 9, &ZS, "F5 包 · G5 肉包 · LFO 两下退出", C_GRAY);
    else if (ui.page != PG_MIX)
        for (i = 0; i < 4; i++) {
            const char *name = 0, *val = 0;
            int32_t x = 4 + i * 59;
            int lit = ui.turned == EN_K1 + i && plat_ms() - ui.turned_at < 400u;
            t[0] = 0;
            if (i == 0) {
                name = "速度";
                int_text(lg_set.bpm, t);
                val = t;
            } else if (i == 1) {
                if (ui.page == PG_SING) {
                    name = "调门";
                    key_name(lg_set.key[cur_qiang()], t);
                    val = t;
                } else {
                    name = "乐器";
                    val = MINE_NAME[lg_set.mine];
                }
            } else if (i == 2) {
                if (ui.page == PG_SING) {
                    name = "板眼";
                    val = lg_set.yan ? "板和眼" : "只打板";
                } else {
                    name = "跟练";
                    val = set.kara ? "开" : "关";
                }
            } else {
                name = "键盘";
                val = KEYS_NAME[lg_set.keys_inst];
            }
            zh_text(x, 2, &ZS, name, C_GRAY);
            zh_text(x, 16, &ZS, val, lit ? C_WHITE : C_AMB);
        }
    else
        zh_text(6, 9, &ZS, "ALGO 换行 · K1-K4 调节 · GLO 返回", C_GRAY);
    cv_blit(0, 208);
}

/* ------------------------------------------------------------------ the logo: the splash, then its intro */
#include "logo.h"
#define INTRO_MS 1100u                           /* the intro: the gong rings this long (a key, a button or a knob
                                                  * ends it) */

static void logo_draw(const uint16_t *pal)      /* the whole screen, each of the logo's pixels 2 x 2 */
{
    uint32_t half, y, x;
    for (half = 0; half < 2u; half++) {
        cv_begin(240, 120, pal[LOGO_BG]);
        for (y = 0; y < LOGO_N / 2u; y++) {
            const uint8_t *row = &LOGO_PX[(half * (LOGO_N / 2u) + y) * (LOGO_N / 2u)];
            uint16_t *d = &cv_px[y * 2u * 240u];
            for (x = 0; x < LOGO_N / 2u; x++) {
                uint16_t a = swap16(pal[row[x] >> 4]), b = swap16(pal[row[x] & 15u]);
                d[4u * x] = d[4u * x + 1u] = d[240u + 4u * x] = d[240u + 4u * x + 1u] = a;
                d[4u * x + 2u] = d[4u * x + 3u] = d[240u + 4u * x + 2u] = d[240u + 4u * x + 3u] = b;
            }
        }
        cv_blit(0, half * 120u);
    }
}

void app_splash(void) { logo_draw(LOGO_PAL); }

/* the intro at t ms: two rings of light run out across the gong's face as if it had just been struck, the second
 * fainter; the strike's sparks flash, then go */
static void intro_frame(uint32_t t)
{
    uint16_t pal[16];
    float p1 = (float)t / 110.0f - 1.0f, p2 = (float)t / 110.0f - 4.5f;   /* where the rings are, in bands */
    int i;
    for (i = 0; i < 16; i++)
        pal[i] = LOGO_PAL[i];
    for (i = 0; i < 6; i++) {
        float d1 = fm_fabsf((float)i - p1), d2 = fm_fabsf((float)i - p2);
        pal[LOGO_BAND + i] = LOGO_PAL[d1 < 0.6f ? LOGO_SHINE : d1 < 1.4f || d2 < 0.6f ? LOGO_GOLD_LT : LOGO_GOLD];
    }
    if (t > 520u || (t / 90u) % 2u)
        pal[LOGO_SPARK] = LOGO_PAL[LOGO_BG];
    logo_draw(pal);
}

void app_ui_init(void)
{
    touch_on(1);
    palette_set(1);                          /* AMBER */
    ui.intro_at = plat_ms() | 1u;            /* (the screen: the splash's logo still; the intro, then the page) */
    ui.intro_drawn = 0;
    ui.touched = 0;
    ui.page = set.page <= PG_SING ? set.page : PG_LUOGU;
    ui.redraw = 15u;
    ui.note_shown = -1;
    ui.turned = -1;
    ui.jp_gm = -1;
    ui.say_shown = -1;
}

void app_frame(void)
{
    static uint32_t last_msg;
    uint32_t now = plat_ms();
    if (ui.intro_at) {                       /* the logo's intro: on till its time is up or anything is touched */
        uint32_t t = now - ui.intro_at;
        input();
        if (t < INTRO_MS && !ui.touched) {
            if (!ui.intro_drawn || now - ui.intro_drawn >= 50u) {
                ui.intro_drawn = now | 1u;
                intro_frame(t);
            }
            return;
        }
        ui.intro_at = 0;
        lcd_fill(0, 0, 240, 240, C_BLACK);
        ui.redraw = 15u;
    } else {
        input();
    }
    if (lg_now.serial != ui.serial) {
        ui.serial = lg_now.serial;
        ui.redraw |= 5u;
    }
    if (lg_now.tq >= 0 && lg_now.seg == SEG_GM && ui.page == PG_SING) {   /* a 唱段: each syllable as it is sung */
        int16_t y = (int16_t)tq_syl_now(lg_tq(lg_now.tq));
        if (y != ui.tq_syl) {
            ui.tq_syl = y;
            ui.redraw |= 4u;
        }
    }
    if (lg_now.beat != ui.shown_beat) {
        ui.shown_beat = lg_now.beat;
        ui.redraw |= 4u;
    }
    if (lg_now.say != ui.say_shown) {        /* the 彩蛋: his bubble with the clip, and a moment after it */
        if (lg_now.say >= 0)
            ui.bubble = lg_now.say;
        else
            ui.bubble_until = now + GUO_HOLD_MS;
        ui.say_shown = lg_now.say;
        ui.redraw |= 6u;
    }
    if (ui.bubble_until && now >= ui.bubble_until && lg_now.say < 0) {
        ui.bubble_until = 0;
        ui.redraw |= 6u;
    }
    if (lg_now.kara_hits != ui.kara_seen[0] || lg_now.kara_miss != ui.kara_seen[1]) {   /* karaoke: the engine's
                                                                                        * verdicts since */
        uint16_t h = lg_now.kara_hits, m = lg_now.kara_miss;
        ui.kara_hits = (uint16_t)(ui.kara_hits + (uint16_t)(h - ui.kara_seen[0]));
        if (m != ui.kara_seen[1]) {          /* a wrong key: said, with what the time waits for */
            char t[24];
            ui.kara_miss = (uint16_t)(ui.kara_miss + (uint16_t)(m - ui.kara_seen[1]));
            kara_want(t, "该打 ");
            app_say("不对", t);
        }
        ui.kara_seen[0] = h;
        ui.kara_seen[1] = m;
    }
    if (ui.kara_run && !playing() && ui.kara_hits + ui.kara_miss) {   /* a karaoke run is over: the tally */
        char t[20] = "对 ";
        int_text(ui.kara_hits, t + len_of(t));
        text_cat(t, " · 错 ");
        int_text(ui.kara_miss, t + len_of(t));
        app_say("打完", t);
        ui.kara_run = 0;
    }
    if (ui.practising && !playing() && lg_score.due) {   /* the run is over: the score */
        int pct = (int)((lg_score.good * 100u + (lg_score.early + lg_score.late) * 50u) /
                        (lg_score.due + lg_score.extra));
        char t[12];
        int_text(pct, t);
        text_cat(t, "%");
        app_say("跟打完", t);
        ui.practising = 0;
    }
    if (lg_score.good + lg_score.early + lg_score.late + lg_score.miss + lg_score.extra != ui.judged) {
        static const char *const J[] = {"", "准", "早", "晚", "漏", "多"};
        ui.judged = (uint16_t)(lg_score.good + lg_score.early + lg_score.late + lg_score.miss + lg_score.extra);
        if (lg_score.last)
            app_say(J[lg_score.last], "");
        ui.redraw |= 4u;
    }
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.redraw |= 1u;
    }
    if (ui.turned >= 0 && now - ui.turned_at >= 400u && now - ui.turned_at < 450u)
        ui.redraw |= 8u;
    if (now - last_msg > 500u) {             /* (the header's state every half second at least) */
        last_msg = now;
        ui.redraw |= 1u;
    }
    if (ui.redraw & 1u)
        draw_header();
    if (ui.redraw & 2u)
        draw_title();
    if (ui.redraw & 4u)
        draw_body();
    if (ui.redraw & 8u)
        draw_footer();
    ui.redraw = 0;
}

#ifdef PLAT_HOST
/* the tests: strokes a pattern owes in 跟练 that no key strikes, for every 乐器 (each such event once a 乐器) */
static int kara_keyless(void)
{
    int n = 0, v, p, sec, i, k;
    for (v = 0; v <= I_NPERC; v++)
        for (p = 0; p < lg_npat(); p++)
            for (sec = 0; sec < 3; sec++) {
                const lg_sec_t *s = &lg_pat(p)->s[sec];
                for (i = 0; i < (int)s->nev; i++) {
                    uint32_t a, b;
                    if (!lg_kara_owes(&s->ev[i], v - 1))
                        continue;
                    for (k = 0; k < NKEYS; k++) {
                        kara_key(k, &a, &b);
                        if (lg_kara_fits(s->ev[i].syl, a, b, v - 1))
                            break;
                    }
                    n += k == NKEYS;
                }
            }
    return n;
}

int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "page"))
        *got = ui.page;
    else if (!strcmp(what, "pat"))
        *got = set.pat;
    else if (!strcmp(what, "bs"))
        *got = set.bs;
    else if (!strcmp(what, "state"))
        *got = lg_now.state;
    else if (!strcmp(what, "seg"))
        *got = lg_now.seg;
    else if (!strcmp(what, "sec"))
        *got = lg_now.sec;
    else if (!strcmp(what, "bpm"))
        *got = lg_set.bpm;
    else if (!strcmp(what, "key"))
        *got = lg_set.key[cur_qiang()];
    else if (!strcmp(what, "mine"))
        *got = lg_set.mine;
    else if (!strcmp(what, "dirty"))
        *got = set_dirty;
    else if (!strcmp(what, "good"))
        *got = lg_score.good;
    else if (!strcmp(what, "miss"))
        *got = lg_score.miss;
    else if (!strcmp(what, "due"))
        *got = lg_score.due;
    else if (!strcmp(what, "extra"))
        *got = lg_score.extra;
    else if (!strcmp(what, "intro"))
        *got = ui.intro_at != 0;
    else if (!strcmp(what, "keydown"))
        *got = ui.key_down;
    else if (!strcmp(what, "kara"))
        *got = set.kara;
    else if (!strcmp(what, "wait"))
        *got = lg_now.wait;
    else if (!strcmp(what, "zhmiss"))
        *got = (int)zh_missing;
    else if (!strcmp(what, "tq"))
        *got = lg_now.tq;
    else if (!strcmp(what, "tqsec"))
        *got = lg_now.tq_sec;
    else if (!strcmp(what, "tqsyl"))
        *got = lg_now.tq >= 0 ? tq_syl_now(lg_tq(lg_now.tq)) : -1;
    else if (!strcmp(what, "keyless"))
        *got = kara_keyless();
    else if (!strcmp(what, "ev"))
        *got = lg_now.ev;
    else if (!strcmp(what, "want"))
        *got = lg_now.want;
    else if (!strcmp(what, "khits"))
        *got = ui.kara_hits;
    else if (!strcmp(what, "kmiss"))
        *got = ui.kara_miss;
    else if (!strcmp(what, "guo"))
        *got = guo_shown();
    else if (!strcmp(what, "say"))
        *got = lg_now.say;
    else if (!strcmp(what, "bubble"))
        *got = guo_said();
    else
        return 0;
    return 1;
}

void app_host_reboot(void)
{
    memset(&ui, 0, sizeof ui);
    set_dirty = 0;
}
#endif
