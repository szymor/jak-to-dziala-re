#include "engine/game.h"
#include "engine/ani_player.h"
#include "formats/dib.h"
#include "formats/dkmm.h"
#include "formats/ne_res.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_POPUPS 8
#define MAX_HISTORY 256
#define BITMAP_CACHE 48
#define STRIP_H 8              /* pop-up title strip (the original used tiny child windows) */

/* Default nav-bar functions, in the order of the template's target list. */
enum { NAV_WORKSHOP, NAV_PRINCIPLES, NAV_AZ, NAV_INVENTORS, NAV_TIMELINE, NAV_OPTIONS, NAV_STORIES, NAV_INDEX,
       NAV_HELP, NAV_BACK };
/* Nav buttons top to bottom -> function (verified by clicking each in the original). */
static const int nav_button_function[9] = {NAV_WORKSHOP, NAV_AZ, NAV_PRINCIPLES, NAV_TIMELINE, NAV_INVENTORS,
                                           NAV_BACK, NAV_INDEX, NAV_OPTIONS, NAV_HELP};

typedef struct Rect {
    int x, y, w, h;
} Rect;

typedef struct CachedDib {
    char path[96];
    Dib dib;
    int ok;
} CachedDib;

enum { END_NONE, END_EVENT6, END_NAV, END_OPEN };

typedef struct Media {
    int kind;                  /* 0 none, 1 ani, 2 wav */
    AniPlayer ani;
    SDL_AudioStream *wav;
    int end;                   /* what happens when it finishes */
    int end_obj, end_child, end_nav;
    uint32_t end_target;       /* END_OPEN: object to open afterwards */
    int skippable;             /* auto-play: a click finishes it (stop + event 6) instead of only stopping */
} Media;

struct Game {
    Vfs *vfs;
    Screen *screen;
    SDL_AudioDeviceID audio;
    uint8_t *png, *exe;
    Dkmm dk;
    NeRes ne;
    int have_ne;
    DkObject **objs;           /* parsed on demand */

    /* layout from the base template */
    Rect nav_panel, page_panel;
    int nav_obj;
    int frame_obj[4];
    Rect frame_rect[4];
    int nframes;
    uint16_t fullscreen_tmpl;  /* kind-2 template: full screen, no nav */
    Rect fullscreen_rect;
    uint32_t nav_targets[9];

    int page, fullscreen;
    Rect page_rect;            /* where the page's coordinates start, and its clip */
    int popups[MAX_POPUPS], npopups;
    int history[MAX_HISTORY], nhistory;

    Media media;

    int dirty;
    CachedDib cache[BITMAP_CACHE];
    int cache_next;
    SDL_Cursor *hand;
    int hover_hand;

    /* A-Z dial (class 4 regions + the page's dial table) */
    int dial_sel, dial_armed;
    Dib exe_ok_lit;            /* EXE bitmap 172: yellow "OK" when a letter is selected */
    int have_ok_lit;

    /* EXE bitmaps named by the data with an '@' prefix (button images), colour-mapped */
    struct { char name[24]; Dib dib; int ok; } exe_bmp[16];
    int nexe_bmp;
};

#define DIAL_OK_X 334          /* where the original draws EXE bitmaps 172/173 on the A-Z page */
#define DIAL_OK_Y 350

/* ------------------------------------------------------------------ helpers */

static DkObject *object(Game *g, int i)
{
    if (i < 0 || i >= g->dk.nobjects)
        return NULL;
    if (!g->objs[i]) {
        DkObject *o = calloc(1, sizeof *o);
        char err[160];
        if (!o || dkmm_object(&g->dk, i, o, err, sizeof err) != 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", err);
            free(o);
            return NULL;
        }
        g->objs[i] = o;
    }
    return g->objs[i];
}

/* Resource name (string ref) relative to the object's directory, or relative to the
 * CD root when it carries its own path ("nav\navb0m0a"). */
static void resolve(Game *g, uint16_t dirref, uint16_t nameref, const char *ext, char *out, size_t outlen)
{
    size_t dl = 0, nl = 0;
    const char *dir = dkmm_string(&g->dk, dirref, &dl), *name = dkmm_string(&g->dk, nameref, &nl);
    char tmp[256];
    if (!name) {
        out[0] = 0;
        return;
    }
    int has_path = memchr(name, '\\', nl) != NULL;
    SDL_snprintf(tmp, sizeof tmp, "%.*s%.*s%s", has_path || !dir ? 0 : (int)dl, dir ? dir : "", (int)nl, name, ext);
    vfs_normalise(tmp, out, outlen);
}

static const Dib *bitmap(Game *g, const char *path)
{
    for (int i = 0; i < BITMAP_CACHE; i++)
        if (g->cache[i].path[0] && !strcmp(g->cache[i].path, path))
            return g->cache[i].ok ? &g->cache[i].dib : NULL;
    CachedDib *c = &g->cache[g->cache_next];
    g->cache_next = (g->cache_next + 1) % BITMAP_CACHE;
    if (c->ok)
        dib_free(&c->dib);
    SDL_strlcpy(c->path, path, sizeof c->path);
    size_t size;
    uint8_t *buf = vfs_read(g->vfs, path, &size);
    char err[128] = "not found";
    c->ok = buf && dib_decode(buf, size, &c->dib, err, sizeof err) == 0;
    if (!c->ok)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s: %s", path, err);
    free(buf);
    return c->ok ? &c->dib : NULL;
}

static const Dib *object_bitmap(Game *g, const DkObject *o)
{
    for (int k = 0; k < o->nchild; k++)
        if (o->children[k].kind == DK_CHILD_BITMAP) {
            char path[256];
            resolve(g, o->dir, o->children[k].file, ".DIB", path, sizeof path);
            return bitmap(g, path);
        }
    return NULL;
}

static void blit_clipped(Screen *s, const Dib *d, int x, int y, Rect clip)
{
    for (int row = 0; row < d->height; row++) {
        int dy = y + row;
        if (dy < clip.y || dy >= clip.y + clip.h || dy < 0 || dy >= SCREEN_H)
            continue;
        int x0 = x < clip.x ? clip.x : x, x1 = x + d->width;
        if (x1 > clip.x + clip.w)
            x1 = clip.x + clip.w;
        if (x0 < 0)
            x0 = 0;
        if (x1 > SCREEN_W)
            x1 = SCREEN_W;
        if (x1 > x0)
            memcpy(s->pixels + (size_t)dy * SCREEN_W + x0, d->pixels + (size_t)row * d->width + (x0 - x),
                   (size_t)(x1 - x0));
    }
    s->dirty = 1;
}

static void fill(Screen *s, int x, int y, int w, int h, uint8_t c)
{
    for (int dy = y; dy < y + h; dy++)
        for (int dx = x; dx < x + w; dx++)
            if (dx >= 0 && dx < SCREEN_W && dy >= 0 && dy < SCREEN_H)
                s->pixels[dy * SCREEN_W + dx] = c;
}

/* The 20 reserved palette slots hold the Windows system colours at run time (WinG identity palette). */
static void install_system_colours(Screen *s)
{
    static const uint8_t sys[20][3] = {
        {0, 0, 0}, {128, 0, 0}, {0, 128, 0}, {128, 128, 0}, {0, 0, 128}, {128, 0, 128}, {0, 128, 128},
        {192, 192, 192}, {192, 220, 192}, {166, 202, 240}, {255, 251, 240}, {160, 160, 164}, {128, 128, 128},
        {255, 0, 0}, {0, 255, 0}, {255, 255, 0}, {0, 0, 255}, {255, 0, 255}, {0, 255, 255}, {255, 255, 255}};
    for (int i = 0; i < 20; i++) {
        int idx = i < 10 ? i : 236 + i;
        s->palette[idx] = (Rgb){sys[i][0], sys[i][1], sys[i][2]};
    }
    s->dirty = 1;
}
enum { SYS_NAVY = 4, SYS_SILVER = 7, SYS_GREY = 248, SYS_WHITE = 255, SYS_BLACK = 0 };

/* EXE bitmaps carry their own palette: map each colour to the nearest game palette entry. */
static void remap_to_screen(const Screen *s, Dib *d)
{
    uint8_t lut[256];
    for (int i = 0; i < 256; i++) {
        int best = 0, bd = 1 << 30;
        for (int j = 0; j < 256; j++) {
            int dr = d->palette[i].r - s->palette[j].r, dg = d->palette[i].g - s->palette[j].g,
                db = d->palette[i].b - s->palette[j].b, dd = dr * dr + dg * dg + db * db;
            if (dd < bd)
                bd = dd, best = j;
        }
        lut[i] = (uint8_t)best;
    }
    for (size_t i = 0, n = (size_t)d->width * d->height; i < n; i++)
        d->pixels[i] = lut[d->pixels[i]];
    memcpy(d->palette, s->palette, sizeof d->palette);
}

/* "@NAME" string ref -> EXE BITMAP resource NAME, mapped to the game palette (cached). */
static const Dib *exe_bitmap(Game *g, uint16_t ref)
{
    size_t n;
    const char *s = dkmm_string(&g->dk, ref, &n);
    if (!s || n < 2 || s[0] != '@' || !g->have_ne)
        return NULL;
    char name[24];
    SDL_snprintf(name, sizeof name, "%.*s", (int)(n - 1), s + 1);
    for (int i = 0; i < g->nexe_bmp; i++)
        if (!SDL_strcasecmp(g->exe_bmp[i].name, name))
            return g->exe_bmp[i].ok ? &g->exe_bmp[i].dib : NULL;
    if (g->nexe_bmp == 16)
        return NULL;
    int i = g->nexe_bmp++;
    SDL_strlcpy(g->exe_bmp[i].name, name, sizeof g->exe_bmp[i].name);
    size_t bl;
    uint8_t *bmp = ne_bitmap_bmp(&g->ne, -1, name, &bl);
    char err[64];
    g->exe_bmp[i].ok = bmp && dib_decode(bmp, bl, &g->exe_bmp[i].dib, err, sizeof err) == 0;
    free(bmp);
    if (g->exe_bmp[i].ok)
        remap_to_screen(g->screen, &g->exe_bmp[i].dib);
    else
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "EXE bitmap %s not found", name);
    return g->exe_bmp[i].ok ? &g->exe_bmp[i].dib : NULL;
}

/* Bitmap buttons (classes 15, 16) are drawn from their EXE images at the hotspot rectangle. */
static void draw_buttons(Game *g, const DkObject *o, int ox, int oy)
{
    Rect all = {0, 0, SCREEN_W, SCREEN_H};
    for (int k = 0; k < o->nchild; k++) {
        const DkChild *c = &o->children[k];
        if (c->cls != 15 && c->cls != 16)
            continue;
        const Dib *d = exe_bitmap(g, c->images[0]);
        if (d)
            blit_clipped(g->screen, d, ox + c->rect[0], oy + c->rect[1], all);
    }
}

static Rect popup_rect(Game *g, const DkObject *o)
{
    int16_t x = (int16_t)o->x, y = (int16_t)o->y;    /* Help has x = -40 */
    return (Rect){g->page_panel.x + x, g->page_panel.y + y, o->width, o->height + STRIP_H};
}

/* Origin of an object's own coordinates (hotspots, animations). */
static void origin(Game *g, int i, int *ox, int *oy)
{
    const DkObject *o = object(g, i);
    if (o && i != g->page && o->type != 2) {
        Rect r = popup_rect(g, o);
        *ox = r.x;
        *oy = r.y + STRIP_H;
    } else {
        *ox = g->page_rect.x;
        *oy = g->page_rect.y;
    }
}

/* ------------------------------------------------------------------ composition */

static void compose(Game *g)
{
    Screen *s = g->screen;
    Rect all = {0, 0, SCREEN_W, SCREEN_H};
    screen_clear(s, SYS_BLACK);
    if (!g->fullscreen) {
        DkObject *nav = object(g, g->nav_obj);
        const Dib *d = nav ? object_bitmap(g, nav) : NULL;
        if (d)
            blit_clipped(s, d, g->nav_panel.x, g->nav_panel.y, g->nav_panel);
    }
    DkObject *page = object(g, g->page);
    const Dib *pd = page ? object_bitmap(g, page) : NULL;
    if (pd)
        blit_clipped(s, pd, g->page_rect.x, g->page_rect.y, g->fullscreen ? all : g->page_rect);
    if (!g->fullscreen)
        for (int k = 0; k < g->nframes; k++) {
            DkObject *f = object(g, g->frame_obj[k]);
            const Dib *d = f ? object_bitmap(g, f) : NULL;
            if (d)
                blit_clipped(s, d, g->frame_rect[k].x, g->frame_rect[k].y, g->frame_rect[k]);
        }
    if (page && page->ntable && g->dial_armed && g->have_ok_lit)
        blit_clipped(s, &g->exe_ok_lit, g->page_rect.x + DIAL_OK_X, g->page_rect.y + DIAL_OK_Y, g->page_rect);
    for (int p = 0; p < g->npopups; p++) {
        DkObject *o = object(g, g->popups[p]);
        if (!o)
            continue;
        Rect r = popup_rect(g, o);
        /* 4-px drop shadow, right and below: black on a checkerboard, the image beneath shows between */
        for (int y = r.y + 4; y < r.y + r.h + 4; y++)
            for (int x = r.x + 4; x < r.x + r.w + 4; x++)
                if ((x >= r.x + r.w || y >= r.y + r.h) && !((x + y) & 1) && x >= 0 && x < SCREEN_W && y >= 0 &&
                    y < SCREEN_H)
                    s->pixels[y * SCREEN_W + x] = SYS_BLACK;   /* every other pixel; the rest shows through */
        fill(s, r.x, r.y, r.w, 1, SYS_BLACK);                     /* title strip: border line ... */
        fill(s, r.x, r.y + 1, r.w, STRIP_H - 1, SYS_NAVY);        /* ... caption ... */
        fill(s, r.x, r.y + 1, STRIP_H - 1, STRIP_H - 1, SYS_SILVER); /* ... system-menu box */
        fill(s, r.x + 1, r.y + 4, STRIP_H - 3, 1, SYS_BLACK);
        const Dib *d = object_bitmap(g, o);
        if (d)
            blit_clipped(s, d, r.x, r.y + STRIP_H, all);
        fill(s, r.x, r.y + STRIP_H, r.w, 1, SYS_BLACK);       /* line under the strip (over the first row) */
        draw_buttons(g, o, r.x, r.y + STRIP_H);
    }
    g->dirty = 0;
}

/* ------------------------------------------------------------------ media */

static void media_stop(Game *g)
{
    if (g->media.kind)
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "media stop");
    if (g->media.kind == 1)
        ani_player_stop(&g->media.ani);
    if (g->media.wav) {
        SDL_DestroyAudioStream(g->media.wav);
        g->media.wav = NULL;
    }
    g->media.kind = 0;
    g->media.end = END_NONE;
    g->media.skippable = 0;
}

static int media_ani(Game *g, const char *path, int ox, int oy)
{
    media_stop(g);
    size_t size;
    uint8_t *buf = vfs_read(g->vfs, path, &size);
    char err[128] = "not found";
    if (!buf || ani_player_start(&g->media.ani, buf, size, g->audio, ox, oy, err, sizeof err) != 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s: %s", path, err);
        return -1;
    }
    g->media.kind = 1;
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "ani %s", path);
    return 0;
}

static int media_wav(Game *g, const char *path)
{
    media_stop(g);
    if (!g->audio)
        return -1;
    size_t size;
    uint8_t *buf = vfs_read(g->vfs, path, &size);
    SDL_AudioSpec spec;
    Uint8 *pcm = NULL;
    Uint32 len = 0;
    SDL_IOStream *io = buf ? SDL_IOFromConstMem(buf, size) : NULL;
    if (!io || !SDL_LoadWAV_IO(io, true, &spec, &pcm, &len)) {      /* decodes MS-ADPCM too */
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s: %s", path, buf ? SDL_GetError() : "not found");
        free(buf);
        return -1;
    }
    free(buf);
    g->media.wav = SDL_CreateAudioStream(&spec, NULL);
    if (g->media.wav && SDL_BindAudioStream(g->audio, g->media.wav)) {
        SDL_PutAudioStreamData(g->media.wav, pcm, (int)len);
        SDL_FlushAudioStream(g->media.wav);
        g->media.kind = 2;
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "wav %s", path);
    }
    SDL_free(pcm);
    return g->media.kind == 2 ? 0 : -1;
}

/* ------------------------------------------------------------------ navigation and actions */

static void run_events(Game *g, int obj, const DkEvent *ev, int n, uint16_t which);
static void open_object(Game *g, uint32_t target, int replace_popup);

static void set_page_layout(Game *g, const DkObject *o)
{
    g->fullscreen = o->tmpl == g->fullscreen_tmpl;
    if (g->fullscreen)
        g->page_rect = g->fullscreen_rect;
    else
        g->page_rect = g->page_panel;
}

static void enter_page(Game *g, int target)
{
    DkObject *o = object(g, target);
    if (!o)
        return;
    media_stop(g);
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "page %d", target);
    g->page = target;
    g->npopups = 0;
    g->dial_sel = 0;              /* verified: the dial starts at A, not armed */
    g->dial_armed = 0;
    set_page_layout(g, o);
    g->dirty = 1;
    compose(g);
    run_events(g, target, o->events, o->nevents, 1);        /* event 1: on open */
    /* auto-play animation (class 7, shape 36): plays now, event 6 when it ends */
    for (int k = 0; k < o->nchild; k++) {
        const DkChild *c = &o->children[k];
        if (c->cls == 7 && c->shape == 36 && c->file) {
            char path[256];
            resolve(g, o->dir, c->file, ".ANI", path, sizeof path);
            if (media_ani(g, path, g->page_rect.x, g->page_rect.y) == 0) {
                g->media.end = END_EVENT6;
                g->media.end_obj = target;
                g->media.end_child = k;
                g->media.skippable = 1;
            }
            break;
        }
    }
}

static void navigate(Game *g, int target)
{
    if (target == g->page && !g->npopups)
        return;
    if (g->page >= 0 && g->nhistory < MAX_HISTORY)
        g->history[g->nhistory++] = g->page;
    enter_page(g, target);
}

static void go_back(Game *g)
{
    if (g->npopups) {             /* a pop-up open: close it first */
        g->npopups--;
        g->dirty = 1;
        return;
    }
    if (g->nhistory > 0)
        enter_page(g, g->history[--g->nhistory]);
}

static void open_object(Game *g, uint32_t target, int replace_popup)
{
    DkObject *o = object(g, (int)target);
    if (!o)
        return;
    if (o->type == 2) {
        navigate(g, (int)target);
        return;
    }
    media_stop(g);
    if (replace_popup && g->npopups)
        g->npopups--;
    for (int p = 0; p < g->npopups; p++)       /* already open: bring to top */
        if (g->popups[p] == (int)target) {
            memmove(g->popups + p, g->popups + p + 1, (size_t)(g->npopups - p - 1) * sizeof *g->popups);
            g->npopups--;
            break;
        }
    if (g->npopups == MAX_POPUPS)
        memmove(g->popups, g->popups + 1, (MAX_POPUPS - 1) * sizeof *g->popups), g->npopups--;
    g->popups[g->npopups++] = (int)target;
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "popup %d", (int)target);
    g->dirty = 1;
    compose(g);
    run_events(g, (int)target, o->events, o->nevents, 1);
}

static void close_object(Game *g, int target)
{
    for (int p = g->npopups - 1; p >= 0; p--)
        if (g->popups[p] == target) {
            memmove(g->popups + p, g->popups + p + 1, (size_t)(g->npopups - p - 1) * sizeof *g->popups);
            g->npopups--;
            g->dirty = 1;
            return;
        }
}

static void play_anim_child(Game *g, int obj, int k)
{
    DkObject *o = object(g, obj);
    if (!o || k < 0 || k >= o->nchild || !o->children[k].file)
        return;
    char path[256];
    resolve(g, o->dir, o->children[k].file, ".ANI", path, sizeof path);
    int ox, oy;
    origin(g, obj, &ox, &oy);
    if (media_ani(g, path, ox, oy) == 0) {
        g->media.end = END_EVENT6;
        g->media.end_obj = obj;
        g->media.end_child = k;
    }
}

static uint32_t arg32(const DkAction *a) { return ((uint32_t)a->args[0] << 16) | a->args[1]; }

static void run_action(Game *g, int obj, const DkAction *a)
{
    DkObject *o = object(g, obj);
    if (!o)
        return;
    char path[256];
    int ox, oy;
    switch (a->type) {
    case 1: open_object(g, arg32(a), 1); break;                     /* open, replacing the top pop-up */
    case 3: open_object(g, arg32(a), 0); break;                     /* open pop-up / go to page */
    case 2:                                                         /* (re)start animation element */
        for (int k = 0; k < o->nchild; k++)
            if (o->children[k].cls == 2 && o->children[k].id == a->args[0]) {
                play_anim_child(g, obj, k);
                break;
            }
        break;
    case 4:                                                         /* play sound */
        resolve(g, o->dir, a->args[0], ".WAV", path, sizeof path);
        media_wav(g, path);
        break;
    case 7:                                                         /* play animation in place */
        resolve(g, o->dir, a->args[0], ".ANI", path, sizeof path);
        origin(g, obj, &ox, &oy);
        media_ani(g, path, ox, oy);
        break;
    case 13: close_object(g, (int)arg32(a)); break;                 /* close that object */
    case 12: break;                                                 /* options radio button: later */
    default: break;
    }
}

static void run_events(Game *g, int obj, const DkEvent *ev, int n, uint16_t which)
{
    for (int i = 0; i < n; i++)
        if (ev[i].event == which)
            for (int k = 0; k < ev[i].nactions; k++)
                run_action(g, obj, &ev[i].actions[k]);
}

static void nav_function(Game *g, int fn)
{
    if (fn == NAV_BACK) {
        go_back(g);
        return;
    }
    uint32_t target = g->nav_targets[fn];
    /* machine articles override the four section buttons with their own links */
    DkObject *page = object(g, g->page);
    if (page && page->type != 0 && page->nlinks > 0 && page->links) {
        if (fn == NAV_PRINCIPLES) target = page->links[0][0];
        if (fn == NAV_AZ) target = page->links[0][1];
        if (fn == NAV_INVENTORS) target = page->links[0][2];
        if (fn == NAV_TIMELINE) target = page->links[0][3];
    }
    open_object(g, target, 0);
}

static void media_finished(Game *g)
{
    int end = g->media.end, obj = g->media.end_obj, child = g->media.end_child, fn = g->media.end_nav;
    media_stop(g);
    if (end == END_NAV) {
        g->dirty = 1;
        nav_function(g, fn);
    } else if (end == END_OPEN) {
        open_object(g, g->media.end_target ? g->media.end_target : 0, 0);
    } else if (end == END_EVENT6) {
        DkObject *o = object(g, obj);
        if (o && child < o->nchild)
            run_events(g, obj, o->children[child].events, o->children[child].nevents, 6);
    }
}

/* ------------------------------------------------------------------ input */

static int hit(const uint16_t r[4], int x, int y)
{
    return r[2] > r[0] && x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3];
}

/* Topmost hotspot of object 'obj' under (x, y) in screen coordinates; -1 if none. */
static int hotspot_at(Game *g, int obj, int x, int y)
{
    DkObject *o = object(g, obj);
    if (!o)
        return -1;
    int ox, oy;
    origin(g, obj, &ox, &oy);
    for (int k = o->nchild - 1; k >= 0; k--) {
        const DkChild *c = &o->children[k];
        if ((c->kind == DK_CHILD_HOTSPOT || c->kind == DK_CHILD_NAVBUTTON) && c->shape != 0 &&
            hit(c->rect, x - ox, y - oy))
            return k;
    }
    return -1;
}

/* A-Z dial region: 0-23 select a letter, 24/25 step left/right, 26 OK (verified in the original:
 * selecting lights the OK button; OK plays the letter's needle animation, then opens its page). */
static void click_dial(Game *g, int obj, const DkObject *o, int value)
{
    int n = o->ntable;
    if (n <= 0)
        return;
    if (value < n) {
        g->dial_sel = value;
        g->dial_armed = 1;
    } else if (value == n) {
        g->dial_sel = (g->dial_sel + n - 1) % n;
        g->dial_armed = 1;
    } else if (value == n + 1) {
        g->dial_sel = (g->dial_sel + 1) % n;
        g->dial_armed = 1;
    } else {
        uint32_t target = o->table_targets[g->dial_sel];
        char path[256];
        resolve(g, o->dir, o->table_anims[g->dial_sel], ".ANI", path, sizeof path);
        if (media_ani(g, path, g->page_rect.x, g->page_rect.y) == 0) {
            g->media.end = END_OPEN;
            g->media.end_target = target;
        } else {
            open_object(g, target, 0);
        }
        return;
    }
    g->dirty = 1;
    (void)obj;
}

static void click_child(Game *g, int obj, int k)
{
    DkObject *o = object(g, obj);
    const DkChild *c = &o->children[k];
    if (c->cls == 4) {
        click_dial(g, obj, o, c->ntail >= 3 ? c->tail[2] : 0);
        return;
    }
    if (c->cls == 7 || (c->cls == 2 && !c->nevents)) {     /* animated hotspot: play, event 6 at the end */
        play_anim_child(g, obj, k);
        return;
    }
    run_events(g, obj, c->events, c->nevents, 4);           /* event 4: click */
}

static int nav_button_at(Game *g, int x, int y)
{
    if (g->fullscreen)
        return -1;
    DkObject *nav = object(g, g->nav_obj);
    if (!nav)
        return -1;
    for (int k = 0, b = 0; k < nav->nchild; k++) {
        const DkChild *c = &nav->children[k];
        if (c->cls != 6)
            continue;
        if (hit(c->rect, x - g->nav_panel.x, y - g->nav_panel.y))
            return b < 9 ? b : -1;
        b++;
    }
    return -1;
}

static int in_rect(Rect r, int x, int y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

void game_click(Game *g, int x, int y)
{
    if (g->media.kind) {
        /* Verified in the original: a click during an auto-play animation (title, film) skips it -
         * it stops and its event 6 fires; a click during media started by a click only stops it. */
        if (g->media.skippable)
            media_finished(g);
        else
            media_stop(g);
        return;
    }
    if (g->npopups) {
        int top = g->popups[g->npopups - 1];
        DkObject *o = object(g, top);
        if (o && in_rect(popup_rect(g, o), x, y)) {
            int k = hotspot_at(g, top, x, y);
            if (k >= 0)
                click_child(g, top, k);
            return;
        }
        g->npopups--;                     /* a click outside closes the pop-up, then goes on */
        g->dirty = 1;
        compose(g);
    }
    int b = nav_button_at(g, x, y);
    if (b >= 0) {
        DkObject *nav = object(g, g->nav_obj);
        int k = 0;
        for (int i = 0, n = 0; i < nav->nchild; i++)
            if (nav->children[i].cls == 6 && n++ == b)
                k = i;
        char path[256];
        resolve(g, 0, nav->children[k].file, ".ANI", path, sizeof path);
        if (media_ani(g, path, g->nav_panel.x, g->nav_panel.y) == 0) {   /* grow animation, then go */
            g->media.end = END_NAV;
            g->media.end_nav = nav_button_function[b];
        } else {
            nav_function(g, nav_button_function[b]);
        }
        return;
    }
    if (in_rect(g->page_rect, x, y)) {
        int k = hotspot_at(g, g->page, x, y);
        if (k >= 0)
            click_child(g, g->page, k);
    }
}

void game_motion(Game *g, int x, int y)
{
    int hand = 0, obj = -1, k = -1;
    if (g->npopups) {
        int top = g->popups[g->npopups - 1];
        DkObject *o = object(g, top);
        if (o && in_rect(popup_rect(g, o), x, y))
            obj = top, k = hotspot_at(g, top, x, y);
    }
    if (obj < 0 && nav_button_at(g, x, y) >= 0)
        hand = 1;
    else if (obj < 0 && in_rect(g->page_rect, x, y))
        obj = g->page, k = hotspot_at(g, g->page, x, y);
    if (k >= 0) {
        size_t n;
        const char *cur = dkmm_string(&g->dk, object(g, obj)->children[k].cursor, &n);
        hand = cur && n == 4 && !SDL_strncasecmp(cur, "Hand", 4);
    }
    g->hover_hand = hand;
}

void game_key(Game *g, SDL_Keycode key)
{
    if (key == SDLK_BACKSPACE || key == SDLK_ESCAPE) {
        if (g->media.kind)
            media_stop(g);
        else if (key == SDLK_ESCAPE && g->npopups)
            go_back(g);
        else if (key == SDLK_BACKSPACE)
            go_back(g);
    }
}

SDL_Cursor *game_cursor(Game *g) { return g->hover_hand ? g->hand : NULL; }
int game_page(const Game *g) { return g->page; }
int game_popup(const Game *g) { return g->npopups ? g->popups[g->npopups - 1] : -1; }

/* ------------------------------------------------------------------ update */

void game_update(Game *g)
{
    if (g->dirty)
        compose(g);
    if (g->media.kind == 1) {
        if (!ani_player_update(&g->media.ani, g->screen))
            media_finished(g);
    } else if (g->media.kind == 2) {
        if (SDL_GetAudioStreamQueued(g->media.wav) <= 0)
            media_finished(g);
    }
}

/* ------------------------------------------------------------------ setup */

static int find_start(Game *g)
{
    for (int i = 0; i < g->dk.nobjects; i++) {
        DkObject *o = object(g, i);
        if (!o || o->type != 2)
            continue;
        for (int k = 0; k < o->nchild; k++)
            if (o->children[k].kind == DK_CHILD_BITMAP) {
                size_t n;
                const char *s = dkmm_string(&g->dk, o->children[k].file, &n);
                if (s && n == 6 && !SDL_strncasecmp(s, "TITL0m", 6))
                    return i;
            }
    }
    return 0;
}

Game *game_create(Vfs *vfs, Screen *screen, SDL_AudioDeviceID audio, char *err, size_t errlen)
{
    Game *g = calloc(1, sizeof *g);
    if (!g)
        return NULL;
    g->vfs = vfs;
    g->screen = screen;
    g->audio = audio;
    g->page = -1;
    size_t size;
    g->png = vfs_read(vfs, "DKCODE/TWTW.PNG", &size);
    if (!g->png || dkmm_open(&g->dk, g->png, size, err, errlen) != 0) {
        if (!g->png)
            SDL_snprintf(err, errlen, "DKCODE/TWTW.PNG not found - is this the right CD?");
        free(g->png);
        free(g);
        return NULL;
    }
    g->objs = calloc((size_t)g->dk.nobjects, sizeof *g->objs);

    /* layout: base template 0 gives the nav panel, the page area (panel 1) and the frame strips */
    g->page_panel = (Rect){94, 4, 540, 450};
    g->nav_obj = -1;
    if (g->dk.nbases) {
        const DkTemplate *b = &g->dk.bases[0];
        for (int k = 0; k < b->npanels; k++) {
            const DkPanel *p = &b->panels[k];
            Rect r = {p->x, p->y, p->w, p->h};
            if (p->name == 1)
                g->page_panel = r;
            else if (p->has_obj && p->kind == 3)
                g->nav_panel = r, g->nav_obj = (int)p->obj;
            else if (p->has_obj && g->nframes < 4)
                g->frame_rect[g->nframes] = r, g->frame_obj[g->nframes++] = (int)p->obj;
        }
    }
    for (int i = 0; i < g->dk.ntemplates; i++)
        if (g->dk.templates[i].kind == 2) {
            const uint16_t *r = g->dk.templates[i].raw_rect;       /* stored as w, h, x, y */
            g->fullscreen_tmpl = g->dk.templates[i].name;
            g->fullscreen_rect = (Rect){r[2], r[3], r[0] + 1, r[1] + 1};
        }
    DkObject *nav = object(g, g->nav_obj);
    if (nav && nav->type == 0)
        for (int k = 0; k < nav->nlinks && k < 9; k++)
            g->nav_targets[k] = nav->tlinks[k][1];

    /* the system palette slots and the shared art palette (from the title bitmap) */
    const Dib *t = bitmap(g, "TITLE/TITL0M.DIB");
    if (t)
        screen_set_palette(screen, t->palette, t->colours);
    install_system_colours(screen);

    size_t exelen;
    g->exe = vfs_read(vfs, "DKCODE/TWTW.EXE", &exelen);
    char nerr[64];
    if (g->exe && ne_open(&g->ne, g->exe, exelen, nerr, sizeof nerr) == 0) {
        g->have_ne = 1;
        size_t bl;
        uint8_t *bmp = ne_bitmap_bmp(&g->ne, 172, NULL, &bl);
        char derr[64];
        if (bmp && dib_decode(bmp, bl, &g->exe_ok_lit, derr, sizeof derr) == 0) {
            remap_to_screen(screen, &g->exe_ok_lit);
            g->have_ok_lit = 1;
        }
        free(bmp);
        NeCursor c;
        if (ne_cursor(&g->ne, 107, &c) == 0) {
            SDL_Surface *s = SDL_CreateSurfaceFrom(c.width, c.height, SDL_PIXELFORMAT_ARGB8888, c.rgba, c.width * 4);
            if (s) {
                g->hand = SDL_CreateColorCursor(s, c.hot_x, c.hot_y);
                SDL_DestroySurface(s);
            }
            ne_cursor_free(&c);
        }
    }
    return g;
}

/* ------------------------------------------------------------------ self-test */

static int check_file(Game *g, int obj, const char *what, const char *path)
{
    if (path[0] && vfs_exists(g->vfs, path))
        return 0;
    printf("object %d: %s missing: %s\n", obj, what, path[0] ? path : "(no name)");
    return 1;
}

static int check_target(Game *g, int obj, const char *what, uint32_t t)
{
    if (object(g, (int)t))
        return 0;
    printf("object %d: %s target %u invalid\n", obj, what, t);
    return 1;
}

static int check_events(Game *g, int obj, const DkObject *o, const DkEvent *ev, int n)
{
    int bad = 0;
    char path[256];
    for (int i = 0; i < n; i++)
        for (int k = 0; k < ev[i].nactions; k++) {
            const DkAction *a = &ev[i].actions[k];
            switch (a->type) {
            case 1: case 3: case 13: bad += check_target(g, obj, "action", arg32(a)); break;
            case 4: resolve(g, o->dir, a->args[0], ".WAV", path, sizeof path); bad += check_file(g, obj, "sound", path); break;
            case 7: resolve(g, o->dir, a->args[0], ".ANI", path, sizeof path); bad += check_file(g, obj, "animation", path); break;
            case 2: {
                int found = 0;
                for (int c = 0; c < o->nchild; c++)
                    found |= o->children[c].cls == 2 && o->children[c].id == a->args[0];
                if (!found) {
                    printf("object %d: action 2 element %u not found\n", obj, a->args[0]);
                    bad++;
                }
                break;
            }
            default: break;
            }
        }
    return bad;
}

int game_selftest(Game *g)
{
    int bad = 0, pages = 0, popups = 0;
    char path[256];
    for (int i = 0; i < g->dk.nobjects; i++) {
        DkObject *o = object(g, i);
        if (!o) {
            printf("object %d: does not parse\n", i);
            bad++;
            continue;
        }
        for (int k = 0; k < o->nchild; k++) {
            const DkChild *c = &o->children[k];
            if (c->kind == DK_CHILD_BITMAP || c->kind == DK_CHILD_CONTENT) {
                resolve(g, o->dir, c->file, ".DIB", path, sizeof path);
                bad += check_file(g, i, "bitmap", path);
                if (vfs_exists(g->vfs, path) && !bitmap(g, path))
                    printf("object %d: bitmap does not decode: %s\n", i, path), bad++;
            }
            if ((c->cls == 2 || c->cls == 6 || c->cls == 7) && c->file) {
                resolve(g, c->cls == 6 ? 0 : o->dir, c->file, ".ANI", path, sizeof path);
                bad += check_file(g, i, "animation", path);
            }
            if (c->cursor && !dkmm_string(&g->dk, c->cursor, NULL))
                printf("object %d: bad cursor ref %u\n", i, c->cursor), bad++;
            bad += check_events(g, i, o, c->events, c->nevents);
        }
        bad += check_events(g, i, o, o->events, o->nevents);
        for (int k = 0; k < o->nlinks && o->type != 0; k++)
            for (int m = 0; m < 4; m++)
                bad += check_target(g, i, "link", o->links[k][m]);
        for (int k = 0; k < o->ntable; k++) {
            bad += check_target(g, i, "dial", o->table_targets[k]);
            resolve(g, o->dir, o->table_anims[k], ".ANI", path, sizeof path);
            bad += check_file(g, i, "dial animation", path);
        }
    }
    /* open every page and pop-up through the engine (exercises layout, composition, events) */
    for (int i = 0; i < g->dk.nobjects; i++) {
        DkObject *o = object(g, i);
        if (!o || o->type == 0)
            continue;
        if (o->type == 2) {
            enter_page(g, i);
            pages++;
        } else {
            open_object(g, (uint32_t)i, 0);
            popups++;
        }
        compose(g);
        media_stop(g);
        g->nhistory = 0;
    }
    printf("self-test: %d objects, %d pages and %d pop-ups opened, %d problems\n", g->dk.nobjects, pages, popups, bad);
    return bad;
}

void game_start(Game *g, int obj)
{
    g->nhistory = 0;
    enter_page(g, obj >= 0 ? obj : find_start(g));
}

void game_destroy(Game *g)
{
    if (!g)
        return;
    media_stop(g);
    for (int i = 0; i < g->dk.nobjects; i++)
        if (g->objs[i]) {
            dkmm_object_free(g->objs[i]);
            free(g->objs[i]);
        }
    free(g->objs);
    for (int i = 0; i < BITMAP_CACHE; i++)
        if (g->cache[i].ok)
            dib_free(&g->cache[i].dib);
    if (g->hand)
        SDL_DestroyCursor(g->hand);
    if (g->have_ok_lit)
        dib_free(&g->exe_ok_lit);
    for (int i = 0; i < g->nexe_bmp; i++)
        if (g->exe_bmp[i].ok)
            dib_free(&g->exe_bmp[i].dib);
    dkmm_close(&g->dk);
    free(g->png);
    free(g->exe);
    free(g);
}
