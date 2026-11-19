/* twtw-tool: headless checks and dumps for the port's loaders.
 *
 *   twtw-tool ls DATA                  list files (DATA = .iso or extracted directory)
 *   twtw-tool check-vfs ISO DIR        compare the ISO reader against an extracted copy
 *   twtw-tool dump-dibs DATA OUTDIR    decode every .DIB to OUTDIR/<path>.idx
 *                                      (format: "W H\n", W*H indices top-down, 768 bytes RGB palette)
 *   twtw-tool dkmm-dump DATA           canonical dump of DKCODE/TWTW.PNG (compare: tools/dkmm.py canonical)
 *   twtw-tool ani-hash DATA            FNV-1a hash of every ANI's decoded frames (compare: tools/ani.py hash)
 *   twtw-tool res-dump DATA OUTDIR     EXE bitmaps as OUTDIR/BITMAP_<id>.bmp, hand cursor as OUTDIR/cursor107.rgba
 */
#include "formats/ani.h"
#include "formats/dib.h"
#include "formats/dkmm.h"
#include "formats/ne_res.h"
#include "vfs/vfs.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Vfs *open_or_die(const char *path)
{
    char err[256];
    Vfs *v = vfs_open(path, err, sizeof err);
    if (!v) {
        fprintf(stderr, "error: %s\n", err);
        exit(1);
    }
    return v;
}

static int ends_with(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && strcmp(s + a - b, suffix) == 0;
}

static int cmd_ls(const char *data)
{
    Vfs *v = open_or_die(data);
    for (size_t i = 0; i < vfs_count(v); i++)
        puts(vfs_name(v, i));
    vfs_close(v);
    return 0;
}

static int cmd_check_vfs(const char *iso, const char *dir)
{
    Vfs *a = open_or_die(iso), *b = open_or_die(dir);
    int bad = 0;
    size_t n = vfs_count(a);
    for (size_t i = 0; i < n; i++) {
        const char *name = vfs_name(a, i);
        size_t sa = 0, sb = 0;
        uint8_t *da = vfs_read(a, name, &sa), *db = vfs_read(b, name, &sb);
        if (!da || !db || sa != sb || memcmp(da, db, sa) != 0) {
            printf("MISMATCH %s (iso %s %zu, dir %s %zu)\n", name, da ? "ok" : "missing", sa,
                   db ? "ok" : "missing", sb);
            bad++;
        }
        free(da);
        free(db);
    }
    printf("%zu files in ISO, %zu in directory, %d mismatches\n", n, vfs_count(b), bad);
    vfs_close(a);
    vfs_close(b);
    return bad ? 1 : 0;
}

static int cmd_dump_dibs(const char *data, const char *outdir)
{
    Vfs *v = open_or_die(data);
    int ok = 0, bad = 0;
    for (size_t i = 0; i < vfs_count(v); i++) {
        const char *name = vfs_name(v, i);
        if (!ends_with(name, ".DIB"))
            continue;
        size_t size;
        uint8_t *buf = vfs_read(v, name, &size);
        Dib dib;
        char err[128];
        if (!buf || dib_decode(buf, size, &dib, err, sizeof err) != 0) {
            printf("FAIL %s: %s\n", name, buf ? err : "read error");
            bad++;
            free(buf);
            continue;
        }
        char path[1024], flat[512];
        SDL_strlcpy(flat, name, sizeof flat);
        for (char *p = flat; *p; p++)
            if (*p == '/')
                *p = '_';
        snprintf(path, sizeof path, "%s/%s.idx", outdir, flat);
        FILE *f = fopen(path, "wb");
        if (f) {
            fprintf(f, "%d %d\n", dib.width, dib.height);
            fwrite(dib.pixels, 1, (size_t)dib.width * dib.height, f);
            fwrite(dib.palette, 1, sizeof dib.palette, f);
            fclose(f);
            ok++;
        } else {
            bad++;
        }
        dib_free(&dib);
        free(buf);
    }
    printf("%d bitmaps decoded, %d failed\n", ok, bad);
    vfs_close(v);
    return bad ? 1 : 0;
}


/* ------------------------------------------------------------------ dkmm-dump */

static void hex(const uint8_t *p, size_t n)
{
    if (!n)
        fputs("-", stdout);
    for (size_t i = 0; i < n; i++)
        printf("%02x", p[i]);
}

static void dump_events(const DkEvent *ev, int n)
{
    for (int i = 0; i < n; i++) {
        printf("e %u %d\n", ev[i].event, ev[i].nactions);
        for (int k = 0; k < ev[i].nactions; k++) {
            printf("a %u", ev[i].actions[k].type);
            for (int m = 0; m < ev[i].actions[k].nargs; m++)
                printf(" %u", ev[i].actions[k].args[m]);
            putchar('\n');
        }
    }
}

static int cmd_dkmm_dump(const char *data)
{
    Vfs *v = open_or_die(data);
    size_t size;
    uint8_t *buf = vfs_read(v, "DKCODE/TWTW.PNG", &size);
    Dkmm dk;
    char err[256];
    if (!buf || dkmm_open(&dk, buf, size, err, sizeof err) != 0) {
        fprintf(stderr, "error: %s\n", buf ? err : "DKCODE/TWTW.PNG not found");
        return 1;
    }
    printf("strings %d\n", dk.nstrings);
    for (int i = 1; i <= dk.nstrings; i++) {
        size_t n;
        const char *str = dkmm_string(&dk, (uint16_t)i, &n);
        printf("s %d ", i);
        hex((const uint8_t *)str, n);
        putchar('\n');
    }
    printf("screen %u %u\n", dk.screen_w, dk.screen_h);
    for (int b = 0; b < dk.nbases; b++) {
        const DkTemplate *t = &dk.bases[b];
        printf("base %u %u %zu %d\n", t->kind, t->name, t->header_len, t->npanels);
        for (int k = 0; k < t->npanels; k++) {
            const DkPanel *p = &t->panels[k];
            printf("p %u %u %u %u ", p->kind, p->name, p->has_obj, p->obj);
            hex(p->rgb, 3);
            printf(" %u %u %u %u\n", p->x, p->y, p->w, p->h);
        }
    }
    for (int i = 0; i < dk.ntemplates; i++) {
        const DkTemplate *t = &dk.templates[i];
        printf("tpl %u %u ", t->kind, t->name);
        hex(t->rgb, 3);
        printf(" %u %u %u %u %d", t->raw_rect[0], t->raw_rect[1], t->raw_rect[2], t->raw_rect[3], t->npanels);
        for (int k = 0; k < t->npanels; k++)
            printf(" %u", t->panel_names[k]);
        putchar('\n');
    }
    for (int i = 0; i < dk.nstyles; i++) {
        const DkStyle *st = &dk.styles[i];
        printf("style %u %u", st->name, st->ref);
        for (int k = 0; k < 4; k++) {
            putchar(' ');
            hex(st->colours[k], 3);
        }
        printf(" %u %s\n", st->value, st->face);
    }
    printf("index %d\n", dk.nindex);
    for (int i = 0; i < dk.nindex; i++) {
        const DkIndexEntry *e = &dk.index[i];
        printf("i %u %u", e->text, e->page);
        for (int k = 0; k < e->npopups; k++)
            printf(" %u", (unsigned)((e->popups[4 * k] << 24) | (e->popups[4 * k + 1] << 16) |
                                     (e->popups[4 * k + 2] << 8) | e->popups[4 * k + 3]));
        putchar('\n');
    }
    printf("objects %d\n", dk.nobjects);
    int bad = 0;
    for (int i = 0; i < dk.nobjects; i++) {
        DkObject o;
        if (dkmm_object(&dk, i, &o, err, sizeof err) != 0) {
            printf("o %d FAIL %s\n", i, err);
            bad++;
            continue;
        }
        printf("o %d %u %u %u %u %u %u %u %u %u %u %u %u %u\n", i, o.type, o.tmpl, o.x, o.y, o.dir, o.w5, o.panel[0],
               o.panel[1], o.panel[2], o.nchildren, o.id, o.width, o.height);
        for (int k = 0; k < o.nchild; k++) {
            const DkChild *c = &o.children[k];
            if (c->kind == DK_CHILD_BITMAP) {
                printf("c bitmap %u\n", c->file);
            } else if (c->kind == DK_CHILD_NAVBUTTON || c->kind == DK_CHILD_CONTENT || c->kind == DK_CHILD_SCROLLBAR ||
                       c->kind == DK_CHILD_LISTCTL) {
                printf("c raw %u", c->cls);
                if (c->kind == DK_CHILD_NAVBUTTON)
                    printf(" %u", c->sub);
                for (int m = 0; m < c->nraw; m++)
                    printf(" %u", c->raw[m]);
                putchar('\n');
            } else {
                printf("c hot %u %u %u %u %u %u %u %u %u %u %u %u %d\n", c->cls, c->sub, c->id, c->shape, c->rect[0],
                       c->rect[1], c->rect[2], c->rect[3], c->cursor, c->flags[0], c->flags[1], c->flags[2], c->nevents);
                dump_events(c->events, c->nevents);
                printf("c tail %d", c->ntail);
                for (int m = 0; m < c->ntail; m++)
                    printf(" %u", c->tail[m]);
                putchar('\n');
                if (c->cls == 2) {
                    printf("c anim %u %u %u %u %u %u", c->anim_rect[0], c->anim_rect[1], c->anim_rect[2],
                           c->anim_rect[3], c->file, c->anim_cursor);
                    for (int m = 0; m < 9; m++)
                        printf(" %u", c->anim_params[m]);
                    putchar('\n');
                } else if (c->cls == 15 || c->cls == 16) {
                    printf("c img %u %u\n", c->images[0], c->images[1]);
                } else if (c->cls == 12) {
                    printf("c style %u\n", c->style);
                } else if (c->cls == 13) {
                    printf("c list ");
                    hex(c->fg, 3);
                    putchar(' ');
                    hex(c->bg, 3);
                    printf(" %s %u %s %u\n", c->source, c->lbflag, c->face, c->style);
                }
            }
        }
        if (o.type == 0) {
            for (int k = 0; k < o.nlinks; k++)
                printf("k %u %u\n", o.tlinks[k][0], o.tlinks[k][1]);
        } else {
            printf("r %d\n", o.nevents);
            dump_events(o.events, o.nevents);
            if (o.ntable) {
                printf("t %d", o.ntable);
                for (int k = 0; k < o.ntable; k++)
                    printf(" %u", o.table_targets[k]);
                for (int k = 0; k < o.ntable; k++)
                    printf(" %u", o.table_anims[k]);
                printf(" %u\n", o.table_extra);
            }
            for (int k = 0; k < o.nlinks; k++)
                printf("k %u %u %u %u\n", o.links[k][0], o.links[k][1], o.links[k][2], o.links[k][3]);
        }
        dkmm_object_free(&o);
    }
    dkmm_close(&dk);
    free(buf);
    vfs_close(v);
    if (bad)
        fprintf(stderr, "%d objects failed to parse\n", bad);
    return bad ? 1 : 0;
}

/* ------------------------------------------------------------------ ani-hash */

static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= 1099511628211ull;
    }
    return h;
}

static int cmd_ani_hash(const char *data)
{
    Vfs *v = open_or_die(data);
    int ok = 0, bad = 0;
    for (size_t i = 0; i < vfs_count(v); i++) {
        const char *name = vfs_name(v, i);
        if (!ends_with(name, ".ANI"))
            continue;
        size_t size;
        uint8_t *buf = vfs_read(v, name, &size);
        Ani ani;
        char err[128];
        if (!buf || ani_open(&ani, buf, size, err, sizeof err) != 0) {
            printf("%s FAIL %s\n", name, buf ? err : "read error");
            bad++;
            free(buf);
            continue;
        }
        uint64_t h = 1469598103934665603ull;
        uint8_t *tmp = malloc(640 * 480 + 2);
        for (int j = 0; j < ani.frames; j++) {
            const AniChunk *c = ani_decode_frame(&ani, j, tmp, 640 * 480 + 2);
            uint8_t hdr[10] = {(uint8_t)j, (uint8_t)(j >> 8)};
            if (!c) {
                h = fnv(h, hdr, 2);
                continue;
            }
            int w = c->x1 - c->x0, hh = c->y1 - c->y0, stride = w + (w & 1);
            uint16_t r[4] = {c->x0, c->y0, c->x1, c->y1};
            for (int k = 0; k < 4; k++)
                hdr[2 + 2 * k] = (uint8_t)r[k], hdr[3 + 2 * k] = (uint8_t)(r[k] >> 8);
            h = fnv(h, hdr, 10);
            for (int y = 0; y < hh; y++)           /* visible pixels only (no stride padding) */
                h = fnv(h, tmp + (size_t)y * stride, (size_t)w);
        }
        uint8_t *pcm = malloc(ani.audio_bytes + 1);
        ani_copy_audio(&ani, pcm);
        uint64_t ha = fnv(1469598103934665603ull, pcm, ani.audio_bytes);
        printf("%s %d %d %d %016llx %u %016llx\n", name, ani.frames, ani.fps, ani.preroll_chunks,
               (unsigned long long)h, ani.audio_bytes, (unsigned long long)ha);
        free(pcm);
        free(tmp);
        ani_close(&ani);
        free(buf);
        ok++;
    }
    fprintf(stderr, "%d animations hashed, %d failed\n", ok, bad);
    vfs_close(v);
    return 0;
}

/* ------------------------------------------------------------------ res-dump */

typedef struct ResDump {
    const NeRes *ne;
    const char *outdir;
    int n;
} ResDump;

static void res_cb(void *user, int type, int id, const char *name, const uint8_t *p, size_t len)
{
    ResDump *r = user;
    if (type != NE_RT_BITMAP)
        return;
    size_t n;
    uint8_t *bmp = ne_bitmap_bmp(r->ne, id, name, &n);
    if (!bmp)
        return;
    char path[1024];
    if (name)
        snprintf(path, sizeof path, "%s/BITMAP_%s.bmp", r->outdir, name);
    else
        snprintf(path, sizeof path, "%s/BITMAP_%d.bmp", r->outdir, id);
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(bmp, 1, n, f);
        fclose(f);
        r->n++;
    }
    Dib dib;
    char err[128];
    if (dib_decode(bmp, n, &dib, err, sizeof err) != 0)
        fprintf(stderr, "decode %s: %s\n", path, err);
    else
        dib_free(&dib);
    free(bmp);
}

static int cmd_res_dump(const char *data, const char *outdir)
{
    Vfs *v = open_or_die(data);
    size_t size;
    uint8_t *buf = vfs_read(v, "DKCODE/TWTW.EXE", &size);
    NeRes ne;
    char err[128];
    if (!buf || ne_open(&ne, buf, size, err, sizeof err) != 0) {
        fprintf(stderr, "error: %s\n", buf ? err : "DKCODE/TWTW.EXE not found");
        return 1;
    }
    ResDump r = {&ne, outdir, 0};
    ne_enum(&ne, res_cb, &r);
    NeCursor cur;
    if (ne_cursor(&ne, 107, &cur) == 0) {
        char path[1024];
        snprintf(path, sizeof path, "%s/cursor107.rgba", outdir);
        FILE *f = fopen(path, "wb");
        if (f) {
            fprintf(f, "%d %d %d %d\n", cur.width, cur.height, cur.hot_x, cur.hot_y);
            fwrite(cur.rgba, 4, (size_t)cur.width * cur.height, f);
            fclose(f);
        }
        printf("cursor 107: %dx%d hotspot (%d,%d)\n", cur.width, cur.height, cur.hot_x, cur.hot_y);
        ne_cursor_free(&cur);
    } else {
        printf("cursor 107 not found\n");
    }
    printf("%d bitmaps written\n", r.n);
    free(buf);
    vfs_close(v);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "ls"))
        return cmd_ls(argv[2]);
    if (argc >= 4 && !strcmp(argv[1], "check-vfs"))
        return cmd_check_vfs(argv[2], argv[3]);
    if (argc >= 4 && !strcmp(argv[1], "dump-dibs"))
        return cmd_dump_dibs(argv[2], argv[3]);
    if (argc >= 3 && !strcmp(argv[1], "dkmm-dump"))
        return cmd_dkmm_dump(argv[2]);
    if (argc >= 3 && !strcmp(argv[1], "ani-hash"))
        return cmd_ani_hash(argv[2]);
    if (argc >= 4 && !strcmp(argv[1], "res-dump"))
        return cmd_res_dump(argv[2], argv[3]);
    fprintf(stderr, "usage: twtw-tool ls DATA | check-vfs ISO DIR | dump-dibs DATA OUTDIR | dkmm-dump DATA |\n"
                    "                 ani-hash DATA | res-dump DATA OUTDIR\n");
    return 2;
}
