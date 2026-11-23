/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "formats/ani.h"
#include "util/bytes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FILE_HDR 0x1e
#define CHUNK_HDR 0x1c

static int fail(char *err, size_t errlen, const char *msg)
{
    if (err && errlen)
        snprintf(err, errlen, "%s", msg);
    return -1;
}

int ani_open(Ani *ani, const uint8_t *d, size_t size, char *err, size_t errlen)
{
    memset(ani, 0, sizeof *ani);
    if (size < FILE_HDR + CHUNK_HDR || rd_le16(d) != 1)
        return fail(err, errlen, "not an ANI file");
    ani->data = d;
    ani->size = size;
    ani->frames = rd_le16(d + 2);
    ani->fps = rd_le16(d + 6);
    ani->preroll_s = rd_le16(d + 8);
    if (rd_le16(d + 0x0e) != 1)
        return fail(err, errlen, "ANI audio is not PCM");
    ani->channels = rd_le16(d + 0x10);
    ani->rate = (int)rd_le32(d + 0x12);
    ani->bits = rd_le16(d + 0x1c);
    if (ani->fps <= 0 || ani->rate <= 0 || ani->bits != 8 || ani->channels != 1)
        return fail(err, errlen, "unsupported ANI parameters");

    int cap = ani->frames + ani->preroll_s * ani->fps + 4;
    ani->chunks = calloc((size_t)cap, sizeof *ani->chunks);
    if (!ani->chunks)
        return fail(err, errlen, "out of memory");
    size_t o = FILE_HDR;
    while (o < size) {
        if (o + CHUNK_HDR > size || memcmp(d + o + 22, "funky!", 6) != 0) {
            ani_close(ani);
            return fail(err, errlen, "bad ANI chunk (no 'funky!' marker)");
        }
        if (ani->nchunks == cap) {
            cap *= 2;
            AniChunk *c = realloc(ani->chunks, (size_t)cap * sizeof *c);
            if (!c) {
                ani_close(ani);
                return fail(err, errlen, "out of memory");
            }
            ani->chunks = c;
        }
        AniChunk *c = &ani->chunks[ani->nchunks];
        c->audio_len = rd_le32(d + o + 2);
        c->x0 = rd_le16(d + o + 6);
        c->y0 = rd_le16(d + o + 8);
        c->x1 = rd_le16(d + o + 10);
        c->y1 = rd_le16(d + o + 12);
        c->raw_len = rd_le32(d + o + 14);
        c->video_len = rd_le32(d + o + 18);
        c->audio_off = (uint32_t)(o + CHUNK_HDR);
        c->video_off = c->audio_off + c->audio_len;
        if ((size_t)c->video_off + c->video_len > size || (c->video_len && (c->x1 < c->x0 || c->y1 < c->y0))) {
            ani_close(ani);
            return fail(err, errlen, "truncated ANI chunk");
        }
        if (c->video_len) {
            uint32_t w = (uint32_t)(c->x1 - c->x0), h = (uint32_t)(c->y1 - c->y0);
            if ((w + (w & 1)) * h != c->raw_len) {
                ani_close(ani);
                return fail(err, errlen, "ANI frame size mismatch");
            }
        }
        ani->audio_bytes += c->audio_len;
        ani->nchunks++;
        o = (size_t)c->video_off + c->video_len;
    }
    ani->preroll_chunks = ani->nchunks - ani->frames;
    if (ani->preroll_chunks < 0) {
        ani_close(ani);
        return fail(err, errlen, "ANI has fewer chunks than frames");
    }
    return 0;
}

void ani_close(Ani *ani)
{
    free(ani->chunks);
    ani->chunks = NULL;
    ani->nchunks = 0;
}

void ani_copy_audio(const Ani *ani, uint8_t *dst)
{
    for (int i = 0; i < ani->nchunks; i++) {
        memcpy(dst, ani->data + ani->chunks[i].audio_off, ani->chunks[i].audio_len);
        dst += ani->chunks[i].audio_len;
    }
}

void ani_bounds(const Ani *ani, int *x0, int *y0, int *x1, int *y1)
{
    int a = 1 << 30, b = 1 << 30, c = 0, e = 0;
    for (int i = 0; i < ani->nchunks; i++) {
        const AniChunk *k = &ani->chunks[i];
        if (!k->video_len)
            continue;
        if (k->x0 < a) a = k->x0;
        if (k->y0 < b) b = k->y0;
        if (k->x1 > c) c = k->x1;
        if (k->y1 > e) e = k->y1;
    }
    if (c == 0)
        a = b = 0;
    *x0 = a, *y0 = b, *x1 = c, *y1 = e;
}

size_t ani_unrle(const uint8_t *s, size_t srclen, uint8_t *dst, size_t n)
{
    size_t i = 0, o = 0;
    while (i < srclen && o < n) {
        uint8_t b = s[i++];
        if (b & 0x80) {
            size_t k = b & 0x7f;
            if (k > srclen - i)
                k = srclen - i;
            if (k > n - o)          /* a few frames run one byte past their size */
                k = n - o;
            memcpy(dst + o, s + i, k);
            i += b & 0x7f;
            o += k;
        } else {
            if (i >= srclen)
                break;
            size_t k = b;
            if (k > n - o)
                k = n - o;
            memset(dst + o, s[i++], k);
            o += k;
        }
    }
    return o;
}

const AniChunk *ani_decode_frame(const Ani *ani, int j, uint8_t *dst, size_t dstlen)
{
    int ci = ani->preroll_chunks + j;
    if (j < 0 || ci >= ani->nchunks)
        return NULL;
    const AniChunk *c = &ani->chunks[ci];
    if (!c->video_len || c->raw_len > dstlen)
        return NULL;
    if (ani_unrle(ani->data + c->video_off, c->video_len, dst, c->raw_len) != c->raw_len)
        return NULL;
    return c;
}

int ani_apply_frame(const Ani *ani, int j, uint8_t *canvas, int cw, int ch, int pitch, int ox, int oy)
{
    int ci = ani->preroll_chunks + j;
    if (j < 0 || ci >= ani->nchunks)
        return -1;
    const AniChunk *c = &ani->chunks[ci];
    if (!c->video_len)
        return 0;
    uint8_t *tmp = malloc(c->raw_len);
    if (!tmp)
        return -1;
    if (!ani_decode_frame(ani, j, tmp, c->raw_len)) {
        free(tmp);
        return -1;
    }
    int w = c->x1 - c->x0, h = c->y1 - c->y0, stride = w + (w & 1);
    for (int y = 0; y < h; y++) {
        int dy = oy + c->y0 + y;
        if (dy < 0 || dy >= ch)
            continue;
        int sx = 0, dx = ox + c->x0, n = w;
        if (dx < 0) {
            sx = -dx;
            n -= sx;
            dx = 0;
        }
        if (dx + n > cw)
            n = cw - dx;
        if (n > 0)
            memcpy(canvas + (size_t)dy * pitch + dx, tmp + (size_t)y * stride + sx, (size_t)n);
    }
    free(tmp);
    return 1;
}
