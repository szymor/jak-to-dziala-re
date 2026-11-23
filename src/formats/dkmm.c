/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "formats/dkmm.h"
#include "util/bytes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(char *err, size_t errlen, const char *fmt, long v)
{
    if (err && errlen)
        snprintf(err, errlen, fmt, v);
    return -1;
}

/* ------------------------------------------------------------------ strings */

const char *dkmm_string(const Dkmm *dk, uint16_t ref, size_t *len)
{
    if (ref == 0 || ref > dk->nstrings)
        return NULL;
    if (len)
        *len = dk->str_len[ref - 1];
    return (const char *)dk->data + dk->str_off[ref - 1];
}

/* ------------------------------------------------------------------ index table */

static int parse_index(Dkmm *dk, size_t o, int store)
{
    const uint8_t *d = dk->data;
    size_t end = dk->objtab_off;
    if (o + 2 > end)
        return -1;
    int n = rd_be16(d + o);
    o += 2;
    if (store) {
        dk->index = calloc((size_t)n, sizeof *dk->index);
        if (!dk->index)
            return -1;
        dk->nindex = n;
    }
    for (int i = 0; i < n; i++) {
        if (o + 12 > end)
            return -1;
        uint16_t ref = rd_be16(d + o), ln = rd_be16(d + o + 2), page = rd_be16(d + o + 8), k = rd_be16(d + o + 10);
        size_t slen;
        if (rd_be32(d + o + 4) != 0 || !dkmm_string(dk, ref, &slen) || slen != ln)
            return -1;
        o += 12;
        if (o + 4u * k > end)
            return -1;
        if (store)
            dk->index[i] = (DkIndexEntry){ref, page, k, d + o};
        o += 4u * k;
    }
    return o == end ? 0 : -1;
}

/* ------------------------------------------------------------------ template section */

static int read_panel(const Dkmm *dk, size_t o, DkPanel *p, size_t *next)
{
    const uint8_t *d = dk->data;
    if (o + 6 > dk->objtab_off)
        return -1;
    p->kind = rd_be16(d + o);
    p->name = rd_be16(d + o + 2);
    p->has_obj = rd_be16(d + o + 4);
    if (p->kind < 1 || p->kind > 3 || p->has_obj > 1)
        return -1;
    size_t q = o + 6;
    p->obj = 0;
    if (p->has_obj) {
        p->obj = rd_be32(d + q);
        q += 4;
    }
    if (q + 11 > dk->objtab_off)
        return -1;
    memcpy(p->rgb, d + q, 3);
    p->w = rd_be16(d + q + 3);
    p->h = rd_be16(d + q + 5);
    p->x = rd_be16(d + q + 7);
    p->y = rd_be16(d + q + 9);
    if (p->w > 640 || p->x > 640 || p->h > 480 || p->y > 480)
        return -1;
    *next = q + 11;
    return 0;
}

static int read_panel_list(const Dkmm *dk, size_t o, DkPanel *out, int *count, size_t *next)
{
    if (o + 2 > dk->objtab_off)
        return -1;
    int n = rd_be16(dk->data + o);
    if (n < 2 || n > 12)
        return -1;
    size_t p = o + 2;
    for (int i = 0; i < n; i++) {
        DkPanel tmp;
        if (read_panel(dk, p, out ? &out[i] : &tmp, &p) != 0)
            return -1;
    }
    *count = n;
    *next = p;
    return 0;
}

/* Style list followed by a final u16 0 that must end exactly at 'end'. */
static int read_styles(Dkmm *dk, size_t o, size_t end, int store)
{
    const uint8_t *d = dk->data;
    if (o + 2 > end)
        return -1;
    int n = rd_be16(d + o);
    size_t p = o + 2;
    if (store) {
        dk->styles = calloc((size_t)(n ? n : 1), sizeof *dk->styles);
        dk->nstyles = n;
    }
    for (int i = 0; i < n; i++) {
        if (p + 28 > end)
            return -1;
        uint16_t ln = rd_be16(d + p + 26);
        for (int k = 18; k < 26; k++)
            if (d[p + k])
                return -1;
        if (ln > 40 || p + 28 + ln + 2 > end)
            return -1;
        if (store) {
            DkStyle *s = &dk->styles[i];
            s->name = rd_be16(d + p);
            s->ref = rd_be16(d + p + 2);
            memcpy(s->colours, d + p + 4, 12);
            s->value = rd_be16(d + p + 16);
            memcpy(s->face, d + p + 28, ln);
            s->face[ln] = 0;
        }
        p += 28 + ln;
        if (d[p] || d[p + 1])
            return -1;
        p += 2;
    }
    return (p == end - 2 && !d[end - 2] && !d[end - 1]) ? 0 : -1;
}

static int parse_templates(Dkmm *dk, size_t end, char *err, size_t errlen)
{
    const uint8_t *d = dk->data;
    size_t o = dk->objtab_off;  /* placeholder, replaced below */
    o = 0;
    /* directory block starts right after the string pool */
    size_t dir = dk->str_off[dk->nstrings - 1] + dk->str_len[dk->nstrings - 1];
    dk->screen_w = rd_be16(d + dir + 2);
    dk->screen_h = rd_be16(d + dir + 4);
    dk->nbases = rd_be16(d + dir + 6);
    dk->bases = calloc((size_t)(dk->nbases ? dk->nbases : 1), sizeof *dk->bases);
    if (!dk->bases)
        return fail(err, errlen, "out of memory", 0);
    o = dir + 8;
    for (int b = 0; b < dk->nbases; b++) {
        size_t p = o, q;
        int n;
        while (read_panel_list(dk, p, NULL, &n, &q) != 0) {
            if (++p >= end)
                return fail(err, errlen, "base template %ld: panel list not found", b);
        }
        DkTemplate *t = &dk->bases[b];
        t->kind = rd_be16(d + o);
        t->name = rd_be16(d + o + 2);
        t->header_len = p - o;
        t->panels = calloc((size_t)n, sizeof *t->panels);
        if (!t->panels)
            return fail(err, errlen, "out of memory", 0);
        read_panel_list(dk, p, t->panels, &t->npanels, &q);
        if (rd_be16(d + q) != 0)
            return fail(err, errlen, "base template %ld trailer", b);
        o = q + 2;
    }
    int cap = 8;
    dk->templates = calloc((size_t)cap, sizeof *dk->templates);
    while (read_styles(dk, o, end, 0) != 0) {
        if (dk->ntemplates == cap) {
            cap *= 2;
            DkTemplate *nt = realloc(dk->templates, (size_t)cap * sizeof *nt);
            if (!nt)
                return fail(err, errlen, "out of memory", 0);
            dk->templates = nt;
        }
        DkTemplate *t = &dk->templates[dk->ntemplates];
        memset(t, 0, sizeof *t);
        t->kind = rd_be16(d + o);
        t->name = rd_be16(d + o + 2);
        size_t p = o + 4;
        if (t->kind == 1 || t->kind == 3) {
            int n = rd_be16(d + p + 40);
            t->npanels = n;
            t->panel_names = calloc((size_t)(n ? n : 1), sizeof *t->panel_names);
            for (int i = 0; i < n; i++)
                t->panel_names[i] = rd_be16(d + p + 42 + 2 * i);
            p += 42 + 2u * n;
        } else if (t->kind == 2) {
            p += 38;
        } else {
            return fail(err, errlen, "unknown template kind at %#lx", (long)o);
        }
        memcpy(t->rgb, d + p + 10, 3);
        for (int i = 0; i < 4; i++)
            t->raw_rect[i] = rd_be16(d + p + 13 + 2 * i);
        size_t pad = t->kind == 2 ? 4 : 2;
        for (size_t i = 0; i < pad; i++)
            if (d[p + 21 + i])
                return fail(err, errlen, "template padding at %#lx", (long)(p + 21));
        o = p + 21 + pad;
        dk->ntemplates++;
        if (o >= end)
            return fail(err, errlen, "template section overran the index table", 0);
    }
    read_styles(dk, o, end, 1);
    return 0;
}

/* ------------------------------------------------------------------ open / close */

int dkmm_open(Dkmm *dk, const uint8_t *d, size_t size, char *err, size_t errlen)
{
    memset(dk, 0, sizeof *dk);
    dk->data = d;
    dk->size = size;
    size_t o = 0;
    for (int i = 0; i < 6; i++) {
        if (o + 2 > size)
            return fail(err, errlen, "truncated header", 0);
        uint16_t n = rd_be16(d + o);
        if (o + 2 + n > size)
            return fail(err, errlen, "truncated header", 0);
        snprintf(dk->header[i], sizeof dk->header[i], "%.*s", n, d + o + 2);
        o += 2u + n;
    }
    if (strcmp(dk->header[0], "DKMM") != 0 || o + 18 > size || memcmp(d + o + 4, "OF", 2) != 0)
        return fail(err, errlen, "not a DKMM page file", 0);
    uint32_t pool_size = rd_be32(d + o + 6);
    dk->objtab_off = rd_be32(d + o + 10);
    size_t pool = o + 14;
    if (dk->objtab_off + 4 > size || pool + pool_size > size)
        return fail(err, errlen, "bad DKMM offsets", 0);
    dk->nstrings = rd_be16(d + pool);
    dk->str_off = malloc((size_t)dk->nstrings * sizeof *dk->str_off);
    dk->str_len = malloc((size_t)dk->nstrings * sizeof *dk->str_len);
    if (!dk->str_off || !dk->str_len)
        return fail(err, errlen, "out of memory", 0);
    size_t p = pool + 2;
    for (int i = 0; i < dk->nstrings; i++) {
        uint16_t n = rd_be16(d + p);
        dk->str_off[i] = (uint32_t)(p + 2);
        dk->str_len[i] = n;
        p += 2u + n;
        if (p > size)
            return fail(err, errlen, "truncated string pool", 0);
    }
    if (p != pool + pool_size)
        return fail(err, errlen, "string pool size mismatch", 0);

    dk->nobjects = (int)rd_be32(d + dk->objtab_off);
    dk->obj_base = dk->objtab_off + 4 + 4u * dk->nobjects;
    if (dk->obj_base > size)
        return fail(err, errlen, "bad object table", 0);
    dk->obj_off = malloc((size_t)dk->nobjects * sizeof *dk->obj_off);
    dk->obj_end = malloc((size_t)dk->nobjects * sizeof *dk->obj_end);
    if (!dk->obj_off || !dk->obj_end)
        return fail(err, errlen, "out of memory", 0);
    for (int i = 0; i < dk->nobjects; i++) {
        dk->obj_off[i] = rd_be32(d + dk->objtab_off + 4 + 4u * i);
        dk->obj_end[i] = (uint32_t)(size - dk->obj_base);
        if (dk->obj_base + dk->obj_off[i] > size)
            return fail(err, errlen, "object %ld out of range", i);
    }
    /* record end = next higher start (offsets are unique; O(n log n) via sort) */
    uint32_t *sorted = malloc((size_t)dk->nobjects * sizeof *sorted);
    if (!sorted)
        return fail(err, errlen, "out of memory", 0);
    memcpy(sorted, dk->obj_off, (size_t)dk->nobjects * sizeof *sorted);
    for (int i = 1; i < dk->nobjects; i++) {           /* insertion sort is fine for 1668 */
        uint32_t v = sorted[i];
        int j = i - 1;
        while (j >= 0 && sorted[j] > v) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = v;
    }
    for (int i = 0; i < dk->nobjects; i++) {
        int lo = 0, hi = dk->nobjects - 1;
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (sorted[mid] <= dk->obj_off[i])
                lo = mid + 1;
            else
                hi = mid;
        }
        if (sorted[lo] > dk->obj_off[i])
            dk->obj_end[i] = sorted[lo];
    }
    free(sorted);

    /* index table: the tail of the directory block that parses exactly to its end */
    size_t dir = p, idx = 0;
    for (size_t q = dir; q + 2 < dk->objtab_off; q++) {
        if (parse_index(dk, q, 0) == 0) {
            idx = q;
            break;
        }
    }
    if (!idx || parse_index(dk, idx, 1) != 0)
        return fail(err, errlen, "index table not found", 0);
    if (parse_templates(dk, idx, err, errlen) != 0)
        return -1;
    return 0;
}

void dkmm_close(Dkmm *dk)
{
    free(dk->str_off);
    free(dk->str_len);
    free(dk->obj_off);
    free(dk->obj_end);
    free(dk->index);
    for (int i = 0; i < dk->nbases; i++)
        free(dk->bases[i].panels);
    for (int i = 0; i < dk->ntemplates; i++)
        free(dk->templates[i].panel_names);
    free(dk->bases);
    free(dk->templates);
    free(dk->styles);
    memset(dk, 0, sizeof *dk);
}

/* ------------------------------------------------------------------ object records */

typedef struct Words {
    const uint8_t *p;
    size_t n, pos;
    int bad;
    char msg[96];
} Words;

static uint16_t get(Words *w)
{
    if (w->pos >= w->n) {
        if (!w->bad)
            snprintf(w->msg, sizeof w->msg, "unexpected end of object");
        w->bad = 1;
        return 0;
    }
    return rd_be16(w->p + 2 * w->pos++);
}

static void getn(Words *w, uint16_t *dst, int n)
{
    for (int i = 0; i < n; i++)
        dst[i] = get(w);
}

static void bad(Words *w, const char *fmt, long v)
{
    if (!w->bad)
        snprintf(w->msg, sizeof w->msg, fmt, v);
    w->bad = 1;
}

static int action_words(uint16_t t)
{
    switch (t) {
    case 1: return 3;
    case 2: return 2;
    case 3: return 3;
    case 4: return 7;
    case 7: return 10;
    case 12: return 1;
    case 13: return 2;
    default: return -1;
    }
}

static DkEvent *parse_events(Words *w, int *count)
{
    int n = get(w);
    *count = 0;
    if (w->bad || n == 0)
        return NULL;
    DkEvent *ev = calloc((size_t)n, sizeof *ev);
    if (!ev) {
        bad(w, "out of memory", 0);
        return NULL;
    }
    *count = n;
    for (int i = 0; i < n && !w->bad; i++) {
        ev[i].event = get(w);
        int na = get(w);
        if (w->bad)
            break;
        ev[i].actions = calloc((size_t)(na ? na : 1), sizeof *ev[i].actions);
        ev[i].nactions = na;
        for (int k = 0; k < na && !w->bad; k++) {
            DkAction *a = &ev[i].actions[k];
            a->type = get(w);
            a->nargs = action_words(a->type);
            if (a->nargs < 0) {
                bad(w, "unknown action type %ld", a->type);
                break;
            }
            getn(w, a->args, a->nargs);
        }
    }
    return ev;
}

static void parse_hotspot(Words *w, DkChild *c)
{
    c->id = get(w);
    c->shape = get(w);
    getn(w, c->rect, 4);
    c->cursor = get(w);
    getn(w, c->flags, 3);
    c->events = parse_events(w, &c->nevents);
}

static void terminator(Words *w)
{
    if (get(w) != 0)
        bad(w, "hotspot terminator", 0);
}

static void parse_child(Words *w, DkChild *c)
{
    memset(c, 0, sizeof *c);
    c->cls = get(w);
    if (c->cls == 10 || c->cls == 9 || c->cls == 14) {
        c->kind = c->cls == 10 ? DK_CHILD_SCROLLBAR : c->cls == 9 ? DK_CHILD_CONTENT : DK_CHILD_LISTCTL;
        c->nraw = c->cls == 10 ? 29 : c->cls == 9 ? 15 : 22;
        getn(w, c->raw, c->nraw);
        c->id = c->raw[0];
        if (c->cls != 14)
            memcpy(c->rect, c->raw + 1, sizeof c->rect);
        if (c->cls == 9)
            c->file = c->raw[14];
        return;
    }
    c->sub = get(w);
    if (c->cls == 6) {
        c->kind = DK_CHILD_NAVBUTTON;
        c->nraw = 20;
        getn(w, c->raw, 20);
        c->id = c->sub;
        memcpy(c->rect, c->raw + 2, sizeof c->rect);
        c->cursor = c->raw[6];
        c->file = c->raw[12];
    } else if (c->cls == 3 && c->sub == 1) {
        c->kind = DK_CHILD_BITMAP;
        uint16_t unk[4];
        getn(w, unk, 4);
        c->file = get(w);
    } else if (c->cls == 2) {
        c->kind = DK_CHILD_HOTSPOT;
        parse_hotspot(w, c);
        terminator(w);
        uint16_t t[16];
        getn(w, t, 16);
        c->tail[0] = t[0];
        c->ntail = 1;
        memcpy(c->anim_rect, t + 1, sizeof c->anim_rect);
        c->file = t[5];
        c->anim_cursor = t[6];
        memcpy(c->anim_params, t + 7, sizeof c->anim_params);
    } else if (c->cls == 12) {
        c->kind = DK_CHILD_HOTSPOT;
        parse_hotspot(w, c);
        terminator(w);
        getn(w, c->tail, 3);
        c->ntail = 3;
        c->style = get(w);
    } else if (c->cls == 13) {
        c->kind = DK_CHILD_HOTSPOT;
        parse_hotspot(w, c);
        terminator(w);
        if (w->bad)
            return;
        const uint8_t *b = w->p + 2 * w->pos;
        size_t avail = 2 * (w->n - w->pos), o = 8;
        if (avail < 10) {
            bad(w, "list box tail truncated", 0);
            return;
        }
        memcpy(c->fg, b, 3);
        memcpy(c->bg, b + 3, 3);
        size_t ls = b[o];
        if (o + 1 + ls + 2 > avail || ls >= sizeof c->source) {
            bad(w, "list box source", 0);
            return;
        }
        memcpy(c->source, b + o + 1, ls);
        o += 1 + ls;
        c->lbflag = b[o++];
        size_t lf = b[o];
        if (o + 1 + lf > avail || lf >= sizeof c->face) {
            bad(w, "list box face", 0);
            return;
        }
        memcpy(c->face, b + o + 1, lf);
        o += 1 + lf;
        if (o % 2) {
            bad(w, "list box tail not word aligned", 0);
            return;
        }
        w->pos += o / 2;
        c->style = get(w);
    } else if (c->cls == 15 || c->cls == 16) {
        c->kind = DK_CHILD_HOTSPOT;
        parse_hotspot(w, c);
        terminator(w);
        uint16_t t[8];
        int n = c->cls == 15 ? 8 : 5;
        getn(w, t, n);
        c->images[0] = t[0];
        c->images[1] = t[1];
        memcpy(c->tail, t + 2, (size_t)(n - 2) * sizeof *t);
        c->ntail = n - 2;
    } else if ((c->cls == 3 || c->cls == 4 || c->cls == 5 || c->cls == 7) &&
               (c->sub == 2 || c->cls == 3 || c->cls == 4)) {
        c->kind = DK_CHILD_HOTSPOT;
        parse_hotspot(w, c);
        terminator(w);
        c->ntail = c->cls == 3 ? 2 : c->cls == 4 ? 3 : c->cls == 5 ? 0 : 8;
        getn(w, c->tail, c->ntail);
        if (c->cls == 7)
            c->file = c->tail[0];
    } else {
        bad(w, "child class %ld not decoded", c->cls);
    }
}

static void free_events(DkEvent *ev, int n)
{
    for (int i = 0; i < n; i++)
        free(ev[i].actions);
    free(ev);
}

void dkmm_object_free(DkObject *o)
{
    for (int i = 0; i < o->nchild; i++)
        free_events(o->children[i].events, o->children[i].nevents);
    free(o->children);
    free_events(o->events, o->nevents);
    free(o->links);
    free(o->tlinks);
    free(o->table_targets);
    free(o->table_anims);
    memset(o, 0, sizeof *o);
}

static uint32_t get32(Words *w)
{
    uint32_t hi = get(w);
    return (hi << 16) | get(w);
}

int dkmm_object(const Dkmm *dk, int i, DkObject *o, char *err, size_t errlen)
{
    memset(o, 0, sizeof *o);
    if (i < 0 || i >= dk->nobjects)
        return fail(err, errlen, "object %ld out of range", i);
    Words W = {dk->data + dk->obj_base + dk->obj_off[i], (dk->obj_end[i] - dk->obj_off[i]) / 2, 0, 0, ""};
    Words *w = &W;
    o->index = i;
    o->type = get(w);
    if (o->type == 0) {
        o->tmpl = get(w);
    } else {
        uint16_t h[8];
        getn(w, h, 8);
        o->tmpl = h[0];
        o->x = h[1];
        o->y = h[2];
        o->dir = h[3];
        o->w5 = h[4];
        memcpy(o->panel, h + 5, sizeof o->panel);
        if (h[5] != 1 || (h[6] != 1 && h[6] != 2))
            bad(w, "irregular header", 0);
    }
    uint16_t core[9];
    getn(w, core, 9);
    if (core[1] != 1 || core[2] != 2 || core[4] || core[5] || core[6])
        bad(w, "irregular header core", 0);
    o->nchildren = core[0];
    o->id = core[3];
    o->width = core[7];
    o->height = core[8];
    if (!w->bad && o->nchildren) {
        o->children = calloc(o->nchildren, sizeof *o->children);
        if (!o->children)
            bad(w, "out of memory", 0);
        for (int k = 0; k < o->nchildren && !w->bad; k++) {
            parse_child(w, &o->children[k]);
            o->nchild = k + 1;
        }
    }
    if (!w->bad && o->type == 0) {
        int nb = 0;
        for (int k = 0; k < o->nchild; k++)
            nb += o->children[k].cls == 6;
        if (get(w) != 0 || get(w) != 0)
            bad(w, "template trailer", 0);
        o->tlinks = calloc((size_t)(nb ? nb : 1), sizeof *o->tlinks);
        o->nlinks = nb;
        for (int k = 0; k < nb; k++)
            o->tlinks[k][0] = get(w);
        for (int k = 0; k < nb; k++)
            o->tlinks[k][1] = get32(w);
    } else if (!w->bad) {
        o->events = parse_events(w, &o->nevents);
        if (get(w) != 0)
            bad(w, "trailer: expected 0", 0);
        int n = get(w);
        if (n && !w->bad) {
            o->ntable = n;
            o->table_targets = calloc((size_t)n, sizeof *o->table_targets);
            o->table_anims = calloc((size_t)n, sizeof *o->table_anims);
            for (int k = 0; k < n; k++)
                o->table_targets[k] = get32(w);
            for (int k = 0; k < n; k++)
                o->table_anims[k] = get(w);
            o->table_extra = get(w);
        }
        int nl = get(w);
        if (nl && !w->bad) {
            o->links = calloc((size_t)nl, sizeof *o->links);
            o->nlinks = nl;
            for (int k = 0; k < nl; k++)
                for (int m = 0; m < 4; m++)
                    o->links[k][m] = get32(w);
        }
    }
    if (!w->bad && w->pos != w->n)
        bad(w, "%ld words left after trailer", (long)(w->n - w->pos));
    if (w->bad) {
        if (err && errlen)
            snprintf(err, errlen, "object %d: %s", i, w->msg);
        dkmm_object_free(o);
        return -1;
    }
    return 0;
}
