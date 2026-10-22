/* SPDX-License-Identifier: MPL-2.0 */
/* Chinese text on the screen: UTF-8 strings drawn from LUOGU's own fonts (dsp/zh_font.h, tools/zh_font.py), into
 * fm1-base's canvas (gfx.c) and blended as its cv_text blends. Part of app.c's unity build, after the platform. */
typedef struct {
    uint8_t h, base;                         /* the line's height, and its baseline from the top */
    uint16_t n;
    const uint16_t *cp;                      /* code points, in order */
    const int8_t *box;                       /* each glyph's ink: x from the pen, y from the line's top, w, h */
    const uint8_t *adv;
    const uint32_t *off;                     /* its coverage: rows of (w + 1) / 2 bytes, 4 bits a pixel */
    const uint8_t *data;
} zh_font_t;
#include "zh_font.h"

/* the strings tools/zh_font.py renders big (ZL) and as titles (ZT): as they are, marked for it */
#define L_(s) s
#define T_(s) s

static uint32_t utf8_next(const char **ps)   /* the next code point (0 at the end; a stray byte reads as '?') */
{
    const uint8_t *s = (const uint8_t *)*ps;
    uint32_t c = *s;
    if (!c)
        return 0;
    if (c < 0x80u) {
        *ps += 1;
        return c;
    }
    if ((c & 0xE0u) == 0xC0u && (s[1] & 0xC0u) == 0x80u) {
        *ps += 2;
        return (c & 0x1Fu) << 6 | (s[1] & 0x3Fu);
    }
    if ((c & 0xF0u) == 0xE0u && (s[1] & 0xC0u) == 0x80u && (s[2] & 0xC0u) == 0x80u) {
        *ps += 3;
        return (c & 0x0Fu) << 12 | (s[1] & 0x3Fu) << 6 | (s[2] & 0x3Fu);
    }
    *ps += 1;
    return '?';
}

static int zh_find(const zh_font_t *f, uint32_t c)   /* the glyph's index, -1 if the font lacks it */
{
    int lo = 0, hi = (int)f->n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (f->cp[mid] == c)
            return mid;
        if (f->cp[mid] < c)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return -1;
}

static uint32_t zh_missing;                     /* characters no font has, drawn as '?' (the tests expect none) */

/* where a character comes from: its font (ZM when the role lacks it, on the same baseline), and its index */
static const zh_font_t *zh_glyph(const zh_font_t *f, uint32_t c, int *gi, int32_t *dy)
{
    *dy = 0;
    if ((*gi = zh_find(f, c)) >= 0)
        return f;
    *dy = (int32_t)f->base - (int32_t)ZM.base;
    if ((*gi = zh_find(&ZM, c)) >= 0)
        return &ZM;
    *gi = zh_find(&ZM, '?');
    if (c != '?')
        zh_missing++;
    return &ZM;
}

static int32_t zh_w(const zh_font_t *f, const char *s)
{
    int32_t w = 0, dy;
    uint32_t c;
    int gi;
    while ((c = utf8_next(&s)) != 0) {
        const zh_font_t *g = zh_glyph(f, c, &gi, &dy);
        if (gi >= 0)
            w += g->adv[gi];
    }
    return w;
}

/* s in colour c, its line's top at y; returns the end x */
static int32_t zh_text(int32_t x, int32_t y, const zh_font_t *f, const char *s, uint16_t c)
{
    uint32_t r = c >> 11, g = (c >> 5) & 63u, b = c & 31u, ch;
    while ((ch = utf8_next(&s)) != 0) {
        int gi;
        int32_t dy, gx, gy, w, h, x0, y0;
        uint32_t bpr;
        const uint8_t *gd;
        const zh_font_t *ft = zh_glyph(f, ch, &gi, &dy);
        if (gi < 0)
            continue;
        x0 = x + ft->box[4 * gi];
        y0 = y + dy + ft->box[4 * gi + 1] + cv_oy;
        w = ft->box[4 * gi + 2];
        h = ft->box[4 * gi + 3];
        bpr = (uint32_t)(w + 1) / 2u;
        gd = ft->data + ft->off[gi];
        for (gy = 0; gy < h; gy++) {
            int32_t py = y0 + gy;
            if (py < cv_y0 || py >= cv_y1)
                continue;
            for (gx = 0; gx < w; gx++) {
                uint32_t a = gd[(uint32_t)gy * bpr + (uint32_t)gx / 2u];
                int32_t px = x0 + gx;
                a = (gx & 1) ? (a & 15u) : (a >> 4);
                if (!a || (uint32_t)px >= cv_w)
                    continue;
                if (a == 15u) {
                    cv_px[(uint32_t)py * cv_w + (uint32_t)px] = swap16(c);
                } else {
                    uint32_t bg = swap16(cv_px[(uint32_t)py * cv_w + (uint32_t)px]);
                    uint32_t br = bg >> 11, bgg = (bg >> 5) & 63u, bb = bg & 31u;
                    uint32_t nr = (r * a + br * (15u - a)) / 15u, ng = (g * a + bgg * (15u - a)) / 15u,
                             nb = (b * a + bb * (15u - a)) / 15u;
                    cv_px[(uint32_t)py * cv_w + (uint32_t)px] = swap16((nr << 11) | (ng << 5) | nb);
                }
            }
        }
        x += ft->adv[gi];
    }
    return x;
}

/* centred on x (its middle), right-aligned at x */
static int32_t zh_text_c(int32_t x, int32_t y, const zh_font_t *f, const char *s, uint16_t c)
{
    return zh_text(x - zh_w(f, s) / 2, y, f, s, c);
}
static int32_t zh_text_r(int32_t x, int32_t y, const zh_font_t *f, const char *s, uint16_t c)
{
    return zh_text(x - zh_w(f, s), y, f, s, c);
}

/* s on up to max_lines lines of at most w pixels from x, y (line_h apart): broken after a space or a punctuation mark
 * when one falls in the line's second half, else between characters; returns the lines used */
static int zh_wrap(int32_t x, int32_t y, const zh_font_t *f, const char *s, uint16_t c, int32_t w, int32_t line_h,
                   int max_lines)
{
    char line[96];
    int n = 0;
    while (*s && n < max_lines) {
        const char *p = s, *brk = 0;
        uint32_t len = 0, i;
        while (*p) {
            const char *q = p;
            uint32_t cp = utf8_next(&q), k = (uint32_t)(q - p);
            if (len + k >= sizeof line)
                break;
            for (i = 0; i < k; i++)
                line[len + i] = p[i];
            line[len + k] = 0;
            if (len && zh_w(f, line) > w)
                break;
            len += k;
            p = q;
            if (cp == ' ' || cp == ',' || cp == ';' || cp == ':' || cp == 0x3001u || cp == 0x3002u || cp == 0xFF0Cu ||
                cp == 0xFF1Au || cp == 0xFF1Bu)
                brk = p;
        }
        if (*p && brk && brk - s > (p - s) / 2) {
            len = (uint32_t)(brk - s);
            p = brk;
        }
        line[len] = 0;
        zh_text(x, y + n * line_h, f, line, c);
        n++;
        s = p;
        while (*s == ' ')
            s++;
    }
    return n;
}
