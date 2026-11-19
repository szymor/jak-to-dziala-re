#include "formats/dib.h"
#include "util/bytes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BI_RGB 0
#define BI_RLE8 1

static int fail(char *err, size_t errlen, const char *msg)
{
    if (err && errlen)
        snprintf(err, errlen, "%s", msg);
    return -1;
}

/* BMP RLE8 into a bottom-up or top-down buffer. Pixels skipped by delta or
 * end-of-line codes keep index 0. 'rows' is the stored row order. */
static int decode_rle8(const uint8_t *s, size_t n, uint8_t *dst, int w, int h, int bottom_up)
{
    int x = 0, y = 0;   /* y counts stored rows */
    size_t i = 0;
#define PUT(v)                                                                 \
    do {                                                                       \
        if (x < w && y < h)                                                    \
            dst[(size_t)(bottom_up ? h - 1 - y : y) * w + x] = (v);            \
        x++;                                                                   \
    } while (0)
    while (i + 1 < n) {
        uint8_t a = s[i++], b = s[i++];
        if (a) {                                       /* encoded run */
            while (a--)
                PUT(b);
        } else if (b == 0) {                           /* end of line */
            x = 0;
            y++;
        } else if (b == 1) {                           /* end of bitmap */
            return 0;
        } else if (b == 2) {                           /* delta */
            if (i + 2 > n)
                return -1;
            x += s[i++];
            y += s[i++];
        } else {                                       /* absolute run, word aligned */
            if (i + b > n)
                return -1;
            for (int k = 0; k < b; k++)
                PUT(s[i + k]);
            i += b + (b & 1);
        }
    }
#undef PUT
    return 0;   /* tolerate a missing end-of-bitmap code */
}

int dib_decode(const uint8_t *d, size_t size, Dib *out, char *err, size_t errlen)
{
    memset(out, 0, sizeof *out);
    if (size < 54 || d[0] != 'B' || d[1] != 'M')
        return fail(err, errlen, "not a BMP file");
    uint32_t bits_off = rd_le32(d + 10);
    uint32_t hdr = rd_le32(d + 14);
    if (hdr < 40 || 14 + hdr > size)
        return fail(err, errlen, "unsupported BMP header");
    int32_t w = (int32_t)rd_le32(d + 18), h = (int32_t)rd_le32(d + 22);
    uint16_t bpp = rd_le16(d + 28);
    uint32_t comp = rd_le32(d + 30), used = rd_le32(d + 46);
    if (bpp != 8)
        return fail(err, errlen, "only 8-bit bitmaps are supported");
    if (comp != BI_RGB && comp != BI_RLE8)
        return fail(err, errlen, "unsupported BMP compression");
    int bottom_up = h > 0;
    if (h < 0)
        h = -h;
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192)
        return fail(err, errlen, "bad BMP dimensions");
    int ncol = used ? (int)used : 256;
    if (ncol > 256 || 14 + hdr + (size_t)ncol * 4 > size || bits_off >= size)
        return fail(err, errlen, "bad BMP palette");
    for (int i = 0; i < ncol; i++) {
        const uint8_t *q = d + 14 + hdr + i * 4;   /* RGBQUAD: B, G, R, reserved */
        out->palette[i] = (Rgb){q[2], q[1], q[0]};
    }
    out->colours = ncol;
    out->width = w;
    out->height = h;
    out->pixels = calloc((size_t)w * h, 1);
    if (!out->pixels)
        return fail(err, errlen, "out of memory");
    const uint8_t *bits = d + bits_off;
    size_t avail = size - bits_off;
    if (comp == BI_RLE8) {
        if (decode_rle8(bits, avail, out->pixels, w, h, bottom_up) != 0) {
            dib_free(out);
            return fail(err, errlen, "corrupt RLE8 data");
        }
    } else {
        size_t stride = ((size_t)w + 3) & ~(size_t)3;
        if (stride * h > avail) {
            dib_free(out);
            return fail(err, errlen, "truncated BMP pixel data");
        }
        for (int y = 0; y < h; y++)
            memcpy(out->pixels + (size_t)(bottom_up ? h - 1 - y : y) * w, bits + stride * y, (size_t)w);
    }
    return 0;
}

void dib_free(Dib *dib)
{
    free(dib->pixels);
    dib->pixels = NULL;
}
