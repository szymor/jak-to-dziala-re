#include "formats/ne_res.h"
#include "util/bytes.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int ne_open(NeRes *ne, const uint8_t *d, size_t size, char *err, size_t errlen)
{
    memset(ne, 0, sizeof *ne);
    if (size < 0x40 || d[0] != 'M' || d[1] != 'Z') {
        snprintf(err, errlen, "not an MZ executable");
        return -1;
    }
    uint32_t hdr = rd_le32(d + 0x3c);
    if (hdr + 0x40 > size || d[hdr] != 'N' || d[hdr + 1] != 'E') {
        snprintf(err, errlen, "not a 16-bit NE executable");
        return -1;
    }
    ne->data = d;
    ne->size = size;
    ne->rt = hdr + rd_le16(d + hdr + 0x24);
    ne->shift = rd_le16(d + ne->rt);
    return 0;
}

static void res_name(const NeRes *ne, uint16_t v, int *id, char *name, size_t namelen)
{
    if (v & 0x8000) {
        *id = v & 0x7fff;
        name[0] = 0;
    } else {
        *id = -1;
        size_t at = ne->rt + v, n = ne->data[at];
        if (n >= namelen)
            n = namelen - 1;
        memcpy(name, ne->data + at + 1, n);
        name[n] = 0;
    }
}

void ne_enum(const NeRes *ne, NeEnumFn cb, void *user)
{
    const uint8_t *d = ne->data;
    size_t o = ne->rt + 2;
    while (o + 8 <= ne->size) {
        uint16_t t = rd_le16(d + o);
        if (t == 0)
            break;
        int count = rd_le16(d + o + 2);
        int type;
        char tname[64];
        res_name(ne, t, &type, tname, sizeof tname);
        o += 8;
        for (int i = 0; i < count && o + 12 <= ne->size; i++, o += 12) {
            size_t off = (size_t)rd_le16(d + o) << ne->shift, len = (size_t)rd_le16(d + o + 2) << ne->shift;
            int id;
            char name[64];
            res_name(ne, rd_le16(d + o + 6), &id, name, sizeof name);
            if (off + len <= ne->size)
                cb(user, type, id, id < 0 ? name : NULL, d + off, len);
        }
    }
}

typedef struct FindCtx {
    int type, id;
    const char *name;
    const uint8_t *p;
    size_t len;
} FindCtx;

static int name_eq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b))
            return 0;
    return *a == *b;
}

static void find_cb(void *user, int type, int id, const char *name, const uint8_t *p, size_t len)
{
    FindCtx *f = user;
    if (f->p || type != f->type)
        return;
    if ((f->name && name && name_eq(f->name, name)) || (!f->name && !name && id == f->id)) {
        f->p = p;
        f->len = len;
    }
}

const uint8_t *ne_find(const NeRes *ne, int type, int id, const char *name, size_t *len)
{
    FindCtx f = {type, id, name, NULL, 0};
    ne_enum(ne, find_cb, &f);
    if (f.p && len)
        *len = f.len;
    return f.p;
}

uint8_t *ne_bitmap_bmp(const NeRes *ne, int id, const char *name, size_t *len)
{
    size_t n;
    const uint8_t *b = ne_find(ne, NE_RT_BITMAP, id, name, &n);
    if (!b || n < 40)
        return NULL;
    uint32_t hs = rd_le32(b);
    uint16_t bpp = rd_le16(b + 14);
    uint32_t ncol = rd_le32(b + 32);
    if (!ncol && bpp <= 8)
        ncol = 1u << bpp;
    uint8_t *out = malloc(14 + n);
    if (!out)
        return NULL;
    uint32_t total = (uint32_t)(14 + n), bits = 14 + hs + 4 * ncol;
    out[0] = 'B';
    out[1] = 'M';
    for (int i = 0; i < 4; i++) {
        out[2 + i] = (uint8_t)(total >> (8 * i));
        out[10 + i] = (uint8_t)(bits >> (8 * i));
    }
    memset(out + 6, 0, 4);
    memcpy(out + 14, b, n);
    if (len)
        *len = 14 + n;
    return out;
}

int ne_cursor(const NeRes *ne, int group_id, NeCursor *out)
{
    memset(out, 0, sizeof *out);
    size_t n;
    const uint8_t *g = ne_find(ne, NE_RT_GROUP_CURSOR, group_id, NULL, &n);
    if (!g || n < 6 + 14 || rd_le16(g + 2) != 2 || rd_le16(g + 4) < 1)
        return -1;
    int cid = rd_le16(g + 6 + 12);                 /* first CURSORDIRENTRY's resource id */
    const uint8_t *c = ne_find(ne, NE_RT_CURSOR, cid, NULL, &n);
    if (!c || n < 4 + 40 + 8)
        return -1;
    out->hot_x = rd_le16(c);
    out->hot_y = rd_le16(c + 2);
    const uint8_t *bi = c + 4;
    int w = (int)rd_le32(bi + 4), h2 = (int)rd_le32(bi + 8), bpp = rd_le16(bi + 14);
    if (bpp != 1 || w <= 0 || w > 64 || h2 <= 0 || h2 > 128)
        return -1;
    int h = h2 / 2, stride = ((w + 31) / 32) * 4;
    const uint8_t *pal = bi + rd_le32(bi), *xorm = pal + 8, *andm = xorm + (size_t)stride * h;
    if (andm + (size_t)stride * h > c + n)
        return -1;
    out->rgba = malloc((size_t)w * h * 4);
    if (!out->rgba)
        return -1;
    out->width = w;
    out->height = h;
    for (int y = 0; y < h; y++) {
        int row = h - 1 - y;                       /* bottom-up */
        for (int x = 0; x < w; x++) {
            int bit = 7 - (x & 7);
            int a = (andm[row * stride + x / 8] >> bit) & 1;
            int v = (xorm[row * stride + x / 8] >> bit) & 1;
            const uint8_t *q = pal + 4 * v;        /* RGBQUAD */
            uint32_t px = a ? 0u : 0xFF000000u | ((uint32_t)q[2] << 16) | ((uint32_t)q[1] << 8) | q[0];
            if (a && v)                            /* "inverted" pixels: show as black */
                px = 0xFF000000u;
            out->rgba[y * w + x] = px;
        }
    }
    return 0;
}

void ne_cursor_free(NeCursor *c)
{
    free(c->rgba);
    c->rgba = NULL;
}
