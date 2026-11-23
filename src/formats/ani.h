/* SPDX-License-Identifier: GPL-3.0-or-later */
/* DK "funky!" ANI animations (see docs/formats/ani.md).
 *
 * An ANI is a header followed by chunks; each chunk carries a slice of
 * unsigned 8-bit mono PCM and optionally one RLE-compressed dirty rectangle.
 * The first preroll_chunks chunks are audio only (sound is stored ahead);
 * chunk preroll_chunks + j carries video frame j.
 */
#ifndef TWTW_ANI_H
#define TWTW_ANI_H

#include <stddef.h>
#include <stdint.h>

typedef struct AniChunk {
    uint32_t audio_off, audio_len;  /* into the file data */
    uint16_t x0, y0, x1, y1;        /* update rectangle, right/bottom exclusive */
    uint32_t raw_len;               /* decoded bytes = stride * height */
    uint32_t video_off, video_len;  /* video_len 0 = no picture change */
} AniChunk;

typedef struct Ani {
    const uint8_t *data;            /* not owned */
    size_t size;
    int frames, fps, preroll_s, preroll_chunks;
    int rate, bits, channels;
    AniChunk *chunks;
    int nchunks;
    uint32_t audio_bytes;           /* total PCM bytes over all chunks */
} Ani;

/* Parses an ANI held in memory (the buffer must outlive the Ani). 0 on success. */
int ani_open(Ani *ani, const uint8_t *data, size_t size, char *err, size_t errlen);
void ani_close(Ani *ani);

/* Copies all PCM into dst (ani->audio_bytes long). */
void ani_copy_audio(const Ani *ani, uint8_t *dst);

/* Bounding box of all video rectangles (x0,y0 inclusive; x1,y1 exclusive). */
void ani_bounds(const Ani *ani, int *x0, int *y0, int *x1, int *y1);

/* Applies video frame j (0-based) to an 8-bit canvas: the frame's rectangle is
 * pasted at (ox + x0, oy + y0), clipped to the canvas. Frames without a picture
 * change leave the canvas untouched. Returns 1 if pixels changed, 0 if not, -1 on error. */
int ani_apply_frame(const Ani *ani, int j, uint8_t *canvas, int cw, int ch, int pitch, int ox, int oy);

/* Decodes the RLE of frame j into dst (raw_len bytes, rows of 'stride' = width rounded up to even).
 * Returns the chunk or NULL if the frame has no picture change. */
const AniChunk *ani_decode_frame(const Ani *ani, int j, uint8_t *dst, size_t dstlen);

/* Byte RLE used by ANI frames: b&0x80 -> copy b&0x7f literal bytes, else repeat next byte b times.
 * Writes at most n bytes; returns the number of bytes produced (n on success). */
size_t ani_unrle(const uint8_t *src, size_t srclen, uint8_t *dst, size_t n);

#endif
