/* SPDX-License-Identifier: MPL-2.0 */
/* The host tools' WAV files: 16-bit PCM, 44.1 kHz, mono or stereo, from floats (+-1); the header written last. */
#pragma once
#include <stdio.h>
#include <stdint.h>

typedef struct {
    FILE *f;
    uint32_t frames, ch;
} wav_t;

static void wav_u32(FILE *f, uint32_t v) { fputc((int)(v & 255u), f), fputc((int)(v >> 8 & 255u), f), fputc((int)(v >> 16 & 255u), f), fputc((int)(v >> 24), f); }
static void wav_u16(FILE *f, uint32_t v) { fputc((int)(v & 255u), f), fputc((int)(v >> 8 & 255u), f); }

static void wav_head(wav_t *w)
{
    uint32_t bytes = w->frames * w->ch * 2u;
    fseek(w->f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, w->f), wav_u32(w->f, 36u + bytes), fwrite("WAVEfmt ", 1, 8, w->f);
    wav_u32(w->f, 16u), wav_u16(w->f, 1u), wav_u16(w->f, w->ch), wav_u32(w->f, 44100u);
    wav_u32(w->f, 44100u * w->ch * 2u), wav_u16(w->f, w->ch * 2u), wav_u16(w->f, 16u);
    fwrite("data", 1, 4, w->f), wav_u32(w->f, bytes);
}

static int wav_open(wav_t *w, const char *path, uint32_t ch)
{
    w->frames = 0;
    w->ch = ch;
    if (!(w->f = fopen(path, "wb")))
        return -1;
    wav_head(w);
    return 0;
}

static void wav_put(wav_t *w, const float *x, uint32_t frames)   /* interleaved */
{
    uint32_t i;
    for (i = 0; i < frames * w->ch; i++) {
        float v = x[i] * 32767.0f;
        int32_t s = v > 32767.0f ? 32767 : v < -32768.0f ? -32768 : (int32_t)(v + (v >= 0.0f ? 0.5f : -0.5f));
        wav_u16(w->f, (uint32_t)(uint16_t)(int16_t)s);
    }
    w->frames += frames;
}

static void wav_close(wav_t *w)
{
    if (!w->f)
        return;
    wav_head(w);
    fclose(w->f);
    w->f = 0;
}
