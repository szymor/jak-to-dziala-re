#include "engine/game.h"
#include "engine/ani_player.h"
#include "formats/dib.h"
#include "formats/dkmm.h"
#include "formats/ne_res.h"
#include "text/cp1250.h"
#include "ui/font.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_POPUPS 8
#define MAX_HISTORY 256
#define BITMAP_CACHE 48
#define STRIP_H 8              /* pop-up title strip (the original used tiny child windows) */

/* Named cursors (FUN_1048_13b6); an element's cursor name is matched case-insensitively
 * (FUN_1030_27ba, _fstricmp): Normal and Arrow = EXE cursor 157 (the game's arrow), Hand = EXE
 * cursor 107, IBeam and Watch = the system I-beam and hourglass. */
enum { CUR_NORMAL, CUR_ARROW, CUR_HAND, CUR_IBEAM, CUR_WATCH };
static const char *const cursor_names[5] = {"Normal", "Arrow", "Hand", "IBeam", "Watch"};

/* Nav buttons (FUN_1048_ebfc): the clicked button's id is looked up in the nav template's id list;
 * its position there (the slot) picks a fixed handler. The template's target list is in article-link
 * order (Workshop, principles, A-Z, inventors, timeline, Options, mammoth stories, Index, Help).
 * Slot 0 Workshop; 1-4 A-Z, principles, timeline, inventors (the article's link 1, 0, 3, 2 if it has
 * links, else the template target); 5 Back; 6 Index; 7 Help; 8 Options. */
enum { NAV_SLOT_WORKSHOP, NAV_SLOT_AZ, NAV_SLOT_PRINCIPLES, NAV_SLOT_TIMELINE, NAV_SLOT_INVENTORS,
       NAV_SLOT_BACK, NAV_SLOT_INDEX, NAV_SLOT_HELP, NAV_SLOT_OPTIONS, NAV_SLOTS };
static const struct { int target, link; } nav_slot[NAV_SLOTS] = {
    {0, -1}, {2, 1}, {1, 0}, {4, 3}, {3, 2}, {-1, -1}, {7, -1}, {8, -1}, {5, -1},
};
#define NAV_TARGET_STORIES 6     /* closed with Options before Options reopens (FUN_1048_ee8c) */

typedef struct Rect {
    int x, y, w, h;
} Rect;

/* A scroll bar (class 10, "ScrollBar" in TWTW.EXE, vtable 1048:c99a) of the top pop-up: over
 * scrolled content (class 9 "ScrollBitmap", help pages) or over the Index list. From the code:
 * arrows 16x16 at both ends, the THUMBTRACK bitmap between them, a 16x16 thumb at
 * top + 16 + round(pos * span / range) with span = h - 47; the content is drawn at
 * round(pos * maxoff / range). Positions run 0..range; arrows step 1, the track steps 'page'. */
typedef struct ScrollBar {
    int bar, content;          /* child indices; content -1 = the Index list */
    int pos, range, page, maxoff;
} ScrollBar;

enum { PART_NONE, PART_UP, PART_DOWN, PART_THUMB, PART_PAGEUP, PART_PAGEDOWN };

/* auto-repeat (FUN_1048_bb3a): a 100 ms timer; the first repeat on the 3rd tick */
#define SB_TICK_NS 100000000ull
#define SB_FIRST_REPEAT_NS (3 * SB_TICK_NS)

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
    int pop_x[MAX_POPUPS], pop_y[MAX_POPUPS];   /* screen position of each open pop-up */
    int history[MAX_HISTORY], nhistory;

    Media media;

    int dirty;
    CachedDib cache[BITMAP_CACHE];
    int cache_next;
    SDL_Cursor *cursor[5];     /* the EXE's named cursors (FUN_1048_13b6), see cursor_names */
    int hover_cursor;

    /* A-Z dial (class 4 regions + the page's dial table) */
    int dial_sel, dial_pressed;
    int dial_lamp, dial_phase; /* OK lamp: -1 not drawn yet, 1 yellow, 0 red; next phase */
    Uint64 dial_next_ns;       /* next lamp toggle (a 1 s timer, FUN_1048_e1c8 / FUN_1048_e302) */
    Dib exe_ok_lit;            /* EXE bitmap 172: yellow "OK" */
    int have_ok_lit;
    Dib exe_ok_dark;           /* EXE bitmap 173: red "OK" */
    int have_ok_dark;
    Dib exe_ok_pressed;        /* EXE bitmap 352: the whole OK button pressed, drawn when OK is clicked */
    int have_ok_pressed;

    /* EXE bitmaps named by the data with an '@' prefix (button images), colour-mapped */
    struct { char name[24]; Dib dib; int ok; } exe_bmp[16];
    int nexe_bmp;

    /* widgets of the top pop-up */
    int widget_obj;            /* pop-up the widget state belongs to (-1 none) */
    ScrollBar sb[4];
    int nsb;
    int press_sb, press_part;
    int btn_obj, btn_k, btn_in, btn_release;   /* bitmap button held down (classes 15, 16) */
    int mouse_x, mouse_y;
    Uint64 press_next_ns;
    Font *font;
    uint8_t col_bg, col_fg, col_sel_bg, col_sel_fg;

    /* Index screen (class 12 edit field + class 13 list) */
    int idx_edit, idx_list, idx_sb;    /* child / scroll bar indices, -1 if not present */
    char idx_text[128];                /* CP1250 */
    int idx_anchor, idx_caret;          /* edit selection [anchor, caret) in bytes; equal = none */
    int idx_sel, idx_top;
    Uint64 idx_last_click_ns;
    int idx_last_row;
};

#define IDX_ROW_H 12           /* list rows are 12 px apart (measured) */

#define DIAL_OK_X 334          /* where the original draws EXE bitmaps 172/173 on the A-Z page */
#define DIAL_OK_Y 350
#define DIAL_PRESSED_X 320     /* FUN_1048_dcfa draws EXE bitmap 352 here on OK */
#define DIAL_PRESSED_Y 331

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

/* The A-Z dial's blinking OK lamp (EXE bitmaps 172 yellow / 173 red at page (334, 350)). */
static void draw_dial_lamp(Game *g)
{
    if (g->dial_pressed || g->dial_lamp < 0)
        return;
    const Dib *lamp = g->dial_lamp ? (g->have_ok_lit ? &g->exe_ok_lit : NULL)
                                   : (g->have_ok_dark ? &g->exe_ok_dark : NULL);
    if (lamp) {
        blit_clipped(g->screen, lamp, g->page_rect.x + DIAL_OK_X, g->page_rect.y + DIAL_OK_Y, g->page_rect);
        g->screen->dirty = 1;
    }
}

/* Bitmap buttons (classes 15, 16) are drawn from their EXE images at the hotspot rectangle:
 * the 'in' image while held down with the pointer on it (FUN_1010_204c / FUN_1010_2210). */
static void draw_buttons(Game *g, int obj, const DkObject *o, int ox, int oy)
{
    Rect all = {0, 0, SCREEN_W, SCREEN_H};
    for (int k = 0; k < o->nchild; k++) {
        const DkChild *c = &o->children[k];
        if (c->cls != 15 && c->cls != 16)
            continue;
        int in = g->btn_obj == obj && g->btn_k == k && g->btn_in;
        const Dib *d = exe_bitmap(g, c->images[in]);
        if (d)
            blit_clipped(g->screen, d, ox + c->rect[0], oy + c->rect[1], all);
    }
}

static Rect popup_rect(Game *g, const DkObject *o)
{
    for (int p = g->npopups - 1; p >= 0; p--)
        if (g->popups[p] == o->index)
            return (Rect){g->pop_x[p], g->pop_y[p], o->width, o->height + STRIP_H};
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

/* ------------------------------------------------------------------ widgets */

static int hit(const uint16_t r[4], int x, int y)
{
    return r[2] > r[0] && x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3];
}

static int in_rect(Rect r, int x, int y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }


static uint8_t nearest_index(const Screen *s, int r, int gg, int b)
{
    int best = 0, bd = 1 << 30;
    for (int j = 0; j < 256; j++) {
        int dr = r - s->palette[j].r, dg = gg - s->palette[j].g, db = b - s->palette[j].b, dd = dr * dr + dg * dg + db * db;
        if (dd < bd)
            bd = dd, best = j;
    }
    return (uint8_t)best;
}

static int idx_count(const Game *g) { return g->dk.nindex; }

static const char *idx_entry(const Game *g, int i, size_t *len)
{
    return dkmm_string(&g->dk, g->dk.index[i].text, len);
}

static int idx_rows(Game *g, const DkObject *o)
{
    const DkChild *l = &o->children[g->idx_list];
    return (l->rect[3] - l->rect[1]) / IDX_ROW_H;
}

static void sb_update_range(Game *g, const DkObject *o, ScrollBar *b)
{
    if (b->content >= 0)
        return;
    int rows = idx_rows(g, o);
    int range = idx_count(g) - rows;
    b->range = range > 0 ? range : 0;
    b->page = rows * 9 / 10 > 0 ? rows * 9 / 10 : 1;   /* the list sets page = trunc(rows * 0.9) */
    b->pos = g->idx_top;
}

/* Prepares the widget state when a pop-up comes to the top. */
static void widgets_setup(Game *g)
{
    int top = g->npopups ? g->popups[g->npopups - 1] : -1;
    if (top == g->widget_obj)
        return;
    g->widget_obj = top;
    g->nsb = 0;
    g->idx_edit = g->idx_list = g->idx_sb = -1;
    g->press_part = PART_NONE;
    if (top < 0)
        return;
    DkObject *o = object(g, top);
    if (!o)
        return;
    for (int k = 0; k < o->nchild; k++) {
        if (o->children[k].cls == 12)
            g->idx_edit = k;
        if (o->children[k].cls == 13)
            g->idx_list = k;
    }
    for (int k = 0; k < o->nchild && g->nsb < 4; k++) {
        const DkChild *c = &o->children[k];
        if (c->cls != 10)
            continue;
        ScrollBar *b = &g->sb[g->nsb];
        memset(b, 0, sizeof *b);
        b->bar = k;
        b->content = -1;
        uint16_t target = c->raw[7];
        for (int m = 0; m < o->nchild; m++)
            if (o->children[m].cls == 9 && o->children[m].id == target)
                b->content = m;
        if (b->content >= 0) {
            const DkChild *v = &o->children[b->content];
            char path[256];
            resolve(g, o->dir, v->file, ".DIB", path, sizeof path);
            const Dib *d = bitmap(g, path);
            int vh = v->rect[3] - v->rect[1];
            b->maxoff = d && d->height > vh ? d->height - vh : 0;
            b->range = (int)((uint32_t)v->raw[10] << 16 | v->raw[11]);   /* the content's position count */
            uint32_t page = (uint32_t)c->raw[27] << 16 | c->raw[28];     /* the bar's page step */
            b->page = page ? (int)page : 1;
        } else if (g->idx_list >= 0) {
            g->idx_sb = g->nsb;
        } else {
            continue;
        }
        g->nsb++;
    }
    if (g->idx_list >= 0) {
        g->idx_text[0] = 0;
        g->idx_anchor = g->idx_caret = 0;
        g->idx_sel = -1;
        g->idx_top = 0;
        g->idx_last_click_ns = 0;
        if (g->idx_sb >= 0)
            sb_update_range(g, o, &g->sb[g->idx_sb]);
    }
}

static Rect sb_rect(const DkObject *o, const ScrollBar *b, int ox, int oy)
{
    const uint16_t *r = o->children[b->bar].rect;
    return (Rect){ox + r[0], oy + r[1], r[2] - r[0], r[3] - r[1]};
}

static int sb_thumb_y(const ScrollBar *b, Rect r)
{
    int travel = r.h - 47;
    return r.y + 16 + (b->range > 0 ? (b->pos * travel * 2 + b->range) / (2 * b->range) : 0);
}

static int sb_offset(const ScrollBar *b)
{
    return b->range > 0 ? (b->pos * b->maxoff * 2 + b->range) / (2 * b->range) : 0;
}

static void draw_scrollbar(Game *g, const DkObject *o, const ScrollBar *b, int ox, int oy)
{
    Rect r = sb_rect(o, b, ox, oy);
    int pressed_up = g->press_part == PART_UP && g->sb + g->press_sb == b;
    int pressed_down = g->press_part == PART_DOWN && g->sb + g->press_sb == b;
    /* the record names its bitmaps: @LINEUP @LINEDOWN @THUMB @THUMBTRACK, then the ...ALT (pressed) set */
    const uint16_t *names = o->children[b->bar].raw + 14;
    /* the track is two parts (page up above the thumb, page down below it), each drawing
     * THUMBTRACK from its own top - the checkered pattern restarts under the thumb */
    int ty = sb_thumb_y(b, r);
    Rect up = {r.x, r.y + 16, r.w, ty - (r.y + 16)};
    Rect down = {r.x, ty + 16, r.w, r.y + r.h - 16 - (ty + 16)};
    const Dib *d;
    if ((d = exe_bitmap(g, names[3]))) {
        blit_clipped(g->screen, d, up.x, up.y, up);
        blit_clipped(g->screen, d, down.x, down.y, down);
    }
    if ((d = exe_bitmap(g, names[2])))
        blit_clipped(g->screen, d, r.x, ty, r);
    if ((d = exe_bitmap(g, names[pressed_up ? 4 : 0])))
        blit_clipped(g->screen, d, r.x, r.y, r);
    if ((d = exe_bitmap(g, names[pressed_down ? 5 : 1])))
        blit_clipped(g->screen, d, r.x, r.y + r.h - 16, r);
}

static void draw_index(Game *g, const DkObject *o, int ox, int oy)
{
    Screen *s = g->screen;
    char utf8[512];
    if (g->idx_edit >= 0) {
        const uint16_t *e = o->children[g->idx_edit].rect;
        Clip c = {ox + e[0] + 1, oy + e[1] + 1, e[2] - e[0] - 2, e[3] - e[1] - 2};
        int ty = oy + e[1] + (e[3] - e[1] - font_line(g->font)) / 2;
        /* text from the field's left edge; the caret is 2 px wide (CreateCaret, FUN_1028_5b8a) */
        int tx = ox + e[0];
        size_t n = cp1250_to_utf8(g->idx_text, strlen(g->idx_text), utf8, sizeof utf8);
        font_draw(g->font, s, utf8, n, tx, ty, c, g->col_fg);
        int a = g->idx_anchor < g->idx_caret ? g->idx_anchor : g->idx_caret;
        int b = g->idx_anchor < g->idx_caret ? g->idx_caret : g->idx_anchor;
        char pre[512];
        size_t pa = cp1250_to_utf8(g->idx_text, (size_t)a, pre, sizeof pre);
        int xa = tx + font_width(g->font, pre, pa);
        if (b > a) {                                     /* the selection, yellow (style 0x0BE3) */
            size_t ns = cp1250_to_utf8(g->idx_text + a, (size_t)(b - a), pre, sizeof pre);
            fill(s, xa, ty, font_width(g->font, pre, ns), font_line(g->font), g->col_sel_bg);
            font_draw(g->font, s, pre, ns, xa, ty, c, g->col_sel_fg);
        }
        size_t pc = cp1250_to_utf8(g->idx_text, (size_t)g->idx_caret, pre, sizeof pre);
        fill(s, tx + font_width(g->font, pre, pc), ty, 2, font_line(g->font), g->col_fg);
    }
    if (g->idx_list >= 0) {
        const uint16_t *l = o->children[g->idx_list].rect;
        int right = ox + l[2];
        if (g->idx_sb >= 0)
            right = ox + o->children[g->sb[g->idx_sb].bar].rect[0];
        int rows = idx_rows(g, o), n = idx_count(g);
        for (int r = 0; r < rows && g->idx_top + r < n; r++) {
            int i = g->idx_top + r, y = oy + l[1] + r * IDX_ROW_H;
            Clip c = {ox + l[0], y, right - (ox + l[0]), IDX_ROW_H};   /* band and text from the left edge */
            if (i == g->idx_sel)
                fill(s, c.x, y, c.w, IDX_ROW_H - 1, g->col_sel_bg);
            size_t len;
            const char *t = idx_entry(g, i, &len);
            size_t u = cp1250_to_utf8(t, len, utf8, sizeof utf8);
            font_draw(g->font, s, utf8, u, ox + l[0], y, c, i == g->idx_sel ? g->col_sel_fg : g->col_fg);
        }
    }
}

/* Draws the widgets of the top pop-up (object o, content origin ox, oy). */
static void draw_widgets(Game *g, const DkObject *o, int ox, int oy)
{
    for (int i = 0; i < g->nsb; i++) {
        ScrollBar *b = &g->sb[i];
        if (b->content >= 0) {
            const DkChild *v = &o->children[b->content];
            char path[256];
            resolve(g, o->dir, v->file, ".DIB", path, sizeof path);
            const Dib *d = bitmap(g, path);
            Rect view = {ox + v->rect[0], oy + v->rect[1], v->rect[2] - v->rect[0], v->rect[3] - v->rect[1]};
            if (d)
                blit_clipped(g->screen, d, view.x, view.y - sb_offset(b), view);
        } else {
            sb_update_range(g, o, b);
        }
        draw_scrollbar(g, o, b, ox, oy);
    }
    if (g->idx_list >= 0)
        draw_index(g, o, ox, oy);
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
    if (page && page->ntable)
        draw_dial_lamp(g);
    if (page && page->ntable && g->dial_pressed && g->have_ok_pressed)
        blit_clipped(s, &g->exe_ok_pressed, g->page_rect.x + DIAL_PRESSED_X, g->page_rect.y + DIAL_PRESSED_Y,
                     g->page_rect);
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
        draw_buttons(g, g->popups[p], o, r.x, r.y + STRIP_H);
        if (p == g->npopups - 1) {
            widgets_setup(g);
            draw_widgets(g, o, r.x, r.y + STRIP_H);
        }
    }
    if (!g->npopups)
        widgets_setup(g);
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
static void options_function(Game *g, int n);
static void idx_ok(Game *g);
enum { OPEN_NEW, OPEN_IN_PLACE };
static void open_object(Game *g, uint32_t target, int mode);

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
    g->dial_sel = 0;              /* the dial panel is rebuilt on each visit: back to A (verified) */
    g->dial_pressed = 0;
    g->dial_lamp = -1;            /* the lamp first lights (yellow) after 1 s, then alternates */
    g->dial_phase = 1;
    g->dial_next_ns = SDL_GetTicksNS() + 1000000000ull;
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
            }
            break;
        }
    }
}

static void navigate(Game *g, int target)
{
    if (target == g->page && !g->npopups)
        return;
    if (g->page >= 0 && target != g->page && g->nhistory < MAX_HISTORY)   /* no repeated entries */
        g->history[g->nhistory++] = g->page;
    enter_page(g, target);
}

/* Back (FUN_1048_ed80 -> FUN_1048_965c): the history holds base pages only (the frame's listener at
 * 1048:2746 records the base page after every CHANGEPAGE); Back changes to the previous one, and
 * does nothing at the bottom. Pop-ups go with the page change. */
static void go_back(Game *g)
{
    if (g->nhistory > 0)
        enter_page(g, g->history[--g->nhistory]);
}

/* OPEN_NEW: action 1 (OPENPAGE, FUN_1010_344a) - a new window on top of the others (verified:
 * a glossary pop-up opened from a zoom pop-up stacks over it); nothing happens if the target is
 * already open, or if its template is single-instance (only the help template, 0x09CB, in this
 * data) and a window of that template is open.
 * OPEN_IN_PLACE: action 3 (CHANGEPAGE, FUN_1028_2a42) - the page changes inside the open window
 * of the target's template (pop-up -> pop-up links always stay within one template), which keeps
 * its position (verified on the help pages and a glossary "Patrz też" link). */
static void open_object(Game *g, uint32_t target, int mode)
{
    int replace_popup = 0;
    DkObject *o = object(g, (int)target);
    if (!o)
        return;
    if (o->type == 2) {
        navigate(g, (int)target);
        return;
    }
    if (mode == OPEN_NEW)
        for (int p = 0; p < g->npopups; p++) {
            DkObject *po = object(g, g->popups[p]);
            if (g->popups[p] == (int)target || (o->tmpl && po && po->tmpl == o->tmpl))
                return;
        }
    media_stop(g);
    int keep_x = 0, keep_y = 0, keep = 0;
    if (g->npopups && mode == OPEN_IN_PLACE) {
        DkObject *topo = object(g, g->popups[g->npopups - 1]);
        if (topo && topo->tmpl == o->tmpl) {
            keep = 1;
            keep_x = g->pop_x[g->npopups - 1];
            keep_y = g->pop_y[g->npopups - 1];
            replace_popup = 1;
        }
    }
    if (replace_popup && g->npopups)
        g->npopups--;
    if (g->npopups == MAX_POPUPS)
    {
        memmove(g->popups, g->popups + 1, (MAX_POPUPS - 1) * sizeof *g->popups);
        memmove(g->pop_x, g->pop_x + 1, (MAX_POPUPS - 1) * sizeof *g->pop_x);
        memmove(g->pop_y, g->pop_y + 1, (MAX_POPUPS - 1) * sizeof *g->pop_y);
        g->npopups--;
    }
    {
        int16_t px = (int16_t)o->x, py = (int16_t)o->y;
        g->pop_x[g->npopups] = keep ? keep_x : g->page_panel.x + px;
        g->pop_y[g->npopups] = keep ? keep_y : g->page_panel.y + py;
    }
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
            memmove(g->pop_x + p, g->pop_x + p + 1, (size_t)(g->npopups - p - 1) * sizeof *g->pop_x);
            memmove(g->pop_y + p, g->pop_y + p + 1, (size_t)(g->npopups - p - 1) * sizeof *g->pop_y);
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
    case 1: open_object(g, arg32(a), OPEN_NEW); break;              /* OPENPAGE: a new window */
    case 3: open_object(g, arg32(a), OPEN_IN_PLACE); break;         /* CHANGEPAGE: page, or pop-up in place */
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
    case 12: options_function(g, a->args[0]); break;                /* Options: copy / print / print setup */
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

/* "Go to" (FUN_1010_53d2): a base page is changed to (CHANGEPAGE), anything else opens in a new
 * window (OPENPAGE); a window already showing the target is dropped first (FUN_1010_31b6). */
static void nav_goto(Game *g, uint32_t target)
{
    close_object(g, (int)target);
    DkObject *o = object(g, (int)target);
    open_object(g, target, o && o->type == 2 ? OPEN_IN_PLACE : OPEN_NEW);
}

static void nav_function(Game *g, int slot)
{
    if (slot < 0 || slot >= NAV_SLOTS)
        return;
    if (slot == NAV_SLOT_BACK) {
        go_back(g);
        return;
    }
    uint32_t target = g->nav_targets[nav_slot[slot].target];
    DkObject *page = object(g, g->page);           /* machine articles override slots 1-4 */
    if (nav_slot[slot].link >= 0 && page && page->type != 0 && page->nlinks > 0 && page->links)
        target = page->links[0][nav_slot[slot].link];
    if (slot == NAV_SLOT_OPTIONS)                  /* Options: close Options and the stories first */
        close_object(g, (int)g->nav_targets[NAV_TARGET_STORIES]);
    nav_goto(g, target);
}

/* The slot of nav button b (top to bottom): its id's position in the template's id list. */
static int nav_slot_of(Game *g, int b)
{
    DkObject *nav = object(g, g->nav_obj);
    if (!nav)
        return -1;
    for (int i = 0, n = 0; i < nav->nchild; i++)
        if (nav->children[i].cls == 6 && n++ == b)
            for (int s = 0; s < nav->nlinks && s < NAV_SLOTS; s++)
                if (nav->tlinks[s][0] == nav->children[i].id)
                    return s;
    return -1;
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

/* ------------------------------------------------------------------ Index screen and options */

static void idx_show(Game *g, const DkObject *o)
{
    int rows = idx_rows(g, o);
    if (g->idx_sel >= 0) {
        if (g->idx_sel >= g->idx_top + rows)
            g->idx_top = g->idx_sel - rows + 1;          /* verified: lands on the last visible row */
        if (g->idx_sel < g->idx_top)
            g->idx_top = g->idx_sel;
    }
    int maxtop = idx_count(g) - rows;
    if (g->idx_top > maxtop)
        g->idx_top = maxtop > 0 ? maxtop : 0;
    if (g->idx_top < 0)
        g->idx_top = 0;
    g->dirty = 1;
}

/* Verified in the original: the selection is the first entry whose Polish sort key is >= the text. */
static void idx_search(Game *g, const DkObject *o)
{
    /* the list controller searches on every edit (FUN_1050_8bda case 1), the empty text too:
     * that selects the first entry and scrolls to the top (verified in the original) */
    size_t tl = strlen(g->idx_text);
    int lo = 0, hi = idx_count(g);
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        size_t n;
        const char *e = idx_entry(g, mid, &n);
        if (cp1250_compare(e, n, g->idx_text, tl) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    g->idx_sel = lo < idx_count(g) ? lo : idx_count(g) - 1;
    idx_show(g, o);
}

/* Selecting an entry in the list puts its text into the edit field, selected (verified). */
static void idx_select(Game *g, const DkObject *o, int i)
{
    int n = idx_count(g);
    if (n <= 0)
        return;
    g->idx_sel = i < 0 ? 0 : i >= n ? n - 1 : i;
    size_t len;
    const char *t = idx_entry(g, g->idx_sel, &len);
    if (len >= sizeof g->idx_text)
        len = sizeof g->idx_text - 1;
    memcpy(g->idx_text, t, len);
    g->idx_text[len] = 0;
    g->idx_anchor = 0;                 /* the entry copied into the field, all selected */
    g->idx_caret = (int)len;
    idx_show(g, o);
}

/* OK / Enter: open the entry's page and its pop-ups. */
static void idx_ok(Game *g)
{
    if (g->idx_sel < 0)
        return;
    const DkIndexEntry *e = &g->dk.index[g->idx_sel];
    int page = e->page, n = e->npopups;
    uint32_t pops[8];
    for (int k = 0; k < n && k < 8; k++)
        pops[k] = ((uint32_t)e->popups[4 * k] << 24) | ((uint32_t)e->popups[4 * k + 1] << 16) |
                  ((uint32_t)e->popups[4 * k + 2] << 8) | e->popups[4 * k + 3];
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "index entry %d -> page %d", g->idx_sel, page);
    g->npopups = 0;
    open_object(g, (uint32_t)page, 0);
    for (int k = 0; k < n && k < 8; k++)
        open_object(g, pops[k], 0);
}

static SDL_Surface *screen_surface(Game *g)
{
    SDL_Surface *s = SDL_CreateSurface(SCREEN_W, SCREEN_H, SDL_PIXELFORMAT_INDEX8);
    if (!s)
        return NULL;
    SDL_Palette *pal = SDL_CreateSurfacePalette(s);
    SDL_Color c[256];
    for (int i = 0; i < 256; i++)
        c[i] = (SDL_Color){g->screen->palette[i].r, g->screen->palette[i].g, g->screen->palette[i].b, 255};
    if (pal)
        SDL_SetPaletteColors(pal, c, 0, 256);
    for (int y = 0; y < SCREEN_H; y++)
        memcpy((uint8_t *)s->pixels + (size_t)y * s->pitch, g->screen->pixels + (size_t)y * SCREEN_W, SCREEN_W);
    return s;
}

typedef struct ClipImage {
    void *png, *bmp;
    size_t npng, nbmp;
} ClipImage;

static const void *SDLCALL clip_data(void *user, const char *mime, size_t *size)
{
    ClipImage *c = user;
    if (!SDL_strcmp(mime, "image/png")) {
        *size = c->npng;
        return c->png;
    }
    *size = c->nbmp;
    return c->bmp;
}

static void SDLCALL clip_free(void *user)
{
    ClipImage *c = user;
    SDL_free(c->png);
    SDL_free(c->bmp);
    SDL_free(c);
}

static void *encode(SDL_Surface *s, int png, size_t *n)
{
    SDL_IOStream *io = SDL_IOFromDynamicMem();
    if (!io)
        return NULL;
    bool ok = png ? SDL_SavePNG_IO(s, io, false) : SDL_SaveBMP_IO(s, io, false);
    void *out = NULL;
    Sint64 size = SDL_GetIOSize(io);
    if (ok && size > 0) {
        void *mem = SDL_GetPointerProperty(SDL_GetIOProperties(io), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, NULL);
        out = SDL_malloc((size_t)size);
        if (out && mem) {
            memcpy(out, mem, (size_t)size);
            *n = (size_t)size;
        }
    }
    SDL_CloseIO(io);
    return out;
}

/* Options: 1 "Kopiuj aktywne okno", 2 "Drukuj aktywne okno", 3 "Ustawienia wydruku".
 * The Options pop-up stays open (verified). Printing is replaced by saving a PNG. */
static void options_function(Game *g, int n)
{
    int top = g->npopups ? g->popups[g->npopups - 1] : -1;
    g->npopups -= top >= 0;                 /* capture the page as the user sees it without the Options box */
    compose(g);
    SDL_Surface *s = screen_surface(g);
    if (top >= 0)
        g->popups[g->npopups++] = top;
    g->dirty = 1;
    if (!s)
        return;
    if (n == 1) {
        ClipImage *c = SDL_calloc(1, sizeof *c);
        c->png = encode(s, 1, &c->npng);
        c->bmp = encode(s, 0, &c->nbmp);
        const char *types[] = {"image/png", "image/bmp"};
        if (!SDL_SetClipboardData(clip_data, clip_free, c, types, 2))
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "clipboard: %s", SDL_GetError());
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "copied the screen to the clipboard");
    } else if (n == 2) {
        const char *dir = SDL_GetUserFolder(SDL_FOLDER_PICTURES);
        char *pref = dir ? NULL : SDL_GetPrefPath("twtw", "Jak to dziala");
        SDL_Time now;
        SDL_DateTime dt;
        SDL_GetCurrentTime(&now);
        SDL_TimeToDateTime(now, &dt, true);
        char path[1024];
        SDL_snprintf(path, sizeof path, "%sjak-to-dziala-%04d%02d%02d-%02d%02d%02d.png", dir ? dir : pref, dt.year,
                     dt.month, dt.day, dt.hour, dt.minute, dt.second);
        SDL_free(pref);
        char msg[1200];
        if (SDL_SavePNG(s, path))
            SDL_snprintf(msg, sizeof msg, "Zapisano obraz strony:\n%s", path);
        else
            SDL_snprintf(msg, sizeof msg, "Nie można zapisać obrazu:\n%s", SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Jak to działa?", msg, NULL);
    } else {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Jak to działa?",
                                 "Drukowanie nie jest dostępne. \"Drukuj aktywne okno\" zapisuje stronę jako obraz PNG.",
                                 NULL);
    }
    SDL_DestroySurface(s);
}

/* Scroll bar hit: returns 1 if (x, y) was on a scroll bar of the top pop-up and handled it. */
static void sb_set(Game *g, const DkObject *o, ScrollBar *b, int pos)
{
    if (pos < 0)
        pos = 0;
    if (pos > b->range)
        pos = b->range;
    b->pos = pos;
    if (b->content < 0) {
        g->idx_top = pos;
        idx_show(g, o);
    }
    g->dirty = 1;
}

/* Which part of bar b is under (x, y), by the original's hit order (FUN_1048_bfce). */
static int sb_part(const ScrollBar *b, Rect r, int x, int y)
{
    if (!in_rect(r, x, y))
        return PART_NONE;
    int ty = sb_thumb_y(b, r);
    if (y < r.y + 16)
        return PART_UP;
    if (y < ty)
        return PART_PAGEUP;
    if (y < ty + 16)
        return PART_THUMB;
    if (y < r.y + r.h - 16)
        return PART_PAGEDOWN;
    return PART_DOWN;
}

static void sb_step(Game *g, const DkObject *o, ScrollBar *b, int part)
{
    int d = part == PART_UP ? -1 : part == PART_DOWN ? 1 : part == PART_PAGEUP ? -b->page : b->page;
    sb_set(g, o, b, b->pos + d);
}

static int sb_click(Game *g, const DkObject *o, int ox, int oy, int x, int y)
{
    for (int i = 0; i < g->nsb; i++) {
        ScrollBar *b = &g->sb[i];
        if (b->content < 0)
            sb_update_range(g, o, b);
        int part = sb_part(b, sb_rect(o, b, ox, oy), x, y);
        if (part == PART_NONE)
            continue;
        g->press_sb = i;
        g->press_part = part;
        if (part != PART_THUMB) {             /* the thumb only starts a drag */
            sb_step(g, o, b, part);
            g->press_next_ns = SDL_GetTicksNS() + SB_FIRST_REPEAT_NS;
        }
        g->dirty = 1;
        return 1;
    }
    return 0;
}

static int widget_click(Game *g, int top, int x, int y)
{
    DkObject *o = object(g, top);
    widgets_setup(g);
    Rect pr = popup_rect(g, o);
    int ox = pr.x, oy = pr.y + STRIP_H;
    if (sb_click(g, o, ox, oy, x, y))
        return 1;
    if (g->idx_list >= 0) {
        const uint16_t *l = o->children[g->idx_list].rect;
        if (hit(l, x - ox, y - oy)) {
            int row = (y - oy - l[1]) / IDX_ROW_H, i = g->idx_top + row;
            if (row < idx_rows(g, o) && i < idx_count(g)) {
                Uint64 now = SDL_GetTicksNS();
                int dbl = i == g->idx_last_row && now - g->idx_last_click_ns < 500000000ull;
                idx_select(g, o, i);
                g->idx_last_row = i;
                g->idx_last_click_ns = now;
                if (dbl)
                    idx_ok(g);
            }
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ input */


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
    } else if (value == n) {
        g->dial_sel = (g->dial_sel + n - 1) % n;
    } else if (value == n + 1) {
        g->dial_sel = (g->dial_sel + 1) % n;
    } else {
        /* OK (FUN_1048_dcfa): stop the lamp timer, draw the pressed button, then the needle */
        g->dial_pressed = 1;
        compose(g);
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
    if ((c->cls == 15 || c->cls == 16) && !g->btn_release) {   /* push button: acts on release */
        g->btn_obj = obj;
        g->btn_k = k;
        g->btn_in = 1;
        g->dirty = 1;
        return;
    }
    if (c->cls == 4) {
        click_dial(g, obj, o, c->ntail >= 3 ? c->tail[2] : 0);
        return;
    }
    if (c->cls == 15 && !c->nevents && g->idx_list >= 0) {   /* the Index screen's OK button */
        idx_ok(g);
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


/* Hotspot flags (the record's 'shape' word, +0x1e in the EXE's hotspot object): */
enum {
    HS_CLICK_THROUGH = 0x01,   /* acts even when its click stopped media (FUN_1010_6f0a) */
    HS_STOPS_MEDIA = 0x04,     /* a click in its window stops all playing media (FUN_1010_bd50) */
    HS_REWIND = 0x10,          /* rewinds its animation when stopped / finished */
    HS_AUTOPLAY = 0x20,        /* class 7: plays when the object opens */
};

/* The window (object) a click at (x, y) is delivered to, and the hotspot hit in it (-1 none). */
static int click_target(Game *g, int x, int y, int *k)
{
    *k = -1;
    if (g->npopups) {
        int top = g->popups[g->npopups - 1];
        DkObject *o = object(g, top);
        if (o && in_rect(popup_rect(g, o), x, y)) {
            *k = hotspot_at(g, top, x, y);
            return top;
        }
        return -1;                       /* outside the pop-up: it closes; nothing else gets the click */
    }
    if (in_rect(g->page_rect, x, y)) {
        *k = hotspot_at(g, g->page, x, y);
        return g->page;
    }
    return -1;
}

void game_click(Game *g, int x, int y)
{
    g->mouse_x = x;
    g->mouse_y = y;
    if (g->media.kind) {
        /* TWTW.EXE (FUN_1010_6f0a / FUN_1010_6420): every hotspot of the clicked window gets the
         * click; one with flag 4 stops all media. If media was stopped, the hotspot under the
         * pointer still acts only with flag 1. Stopping never fires event 6: auto-play pages
         * "skip" through flag-1 hotspots whose click repeats the finish target (verified in the
         * original: a click on a film's frame goes to the frame's target, not the film's).
         * The title and credits have no flag-4 hotspot: the click goes through and the page
         * change stops the animation. Nav buttons: stop only (verified). */
        int k, obj = click_target(g, x, y, &k);
        DkObject *o = obj >= 0 ? object(g, obj) : NULL;
        int stops = obj < 0;             /* nav panel / outside a pop-up: stop (verified) */
        for (int i = 0; o && i < o->nchild && !stops; i++)
            if (o->children[i].kind == DK_CHILD_HOTSPOT && (o->children[i].shape & HS_STOPS_MEDIA))
                stops = 1;
        if (!stops)
            goto deliver;
        media_stop(g);
        if (o && k >= 0 && (o->children[k].shape & HS_CLICK_THROUGH) && obj == click_target(g, x, y, &k))
            click_child(g, obj, k);
        return;
    }
deliver:
    /* A click outside the top pop-up closes it, and so on down the stack until the click is inside
     * one or none is left; then it goes on (verified with a glossary pop-up over a zoom pop-up). */
    while (g->npopups) {
        int top = g->popups[g->npopups - 1];
        DkObject *o = object(g, top);
        if (o && in_rect(popup_rect(g, o), x, y)) {
            if (widget_click(g, top, x, y))
                return;
            int k = hotspot_at(g, top, x, y);
            if (k >= 0)
                click_child(g, top, k);
            return;
        }
        g->npopups--;
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
            g->media.end_nav = nav_slot_of(g, b);
        } else {
            nav_function(g, nav_slot_of(g, b));
        }
        return;
    }
    if (in_rect(g->page_rect, x, y)) {
        int k = hotspot_at(g, g->page, x, y);
        if (k >= 0)
            click_child(g, g->page, k);
    }
}

void game_mouse_up(Game *g)
{
    if (g->btn_obj >= 0) {                      /* FUN_1010_210c: click only if released on the button */
        int obj = g->btn_obj, k = g->btn_k, in = g->btn_in;
        g->btn_obj = -1;
        g->btn_in = 0;
        g->dirty = 1;
        if (in && object(g, obj)) {
            g->btn_release = 1;
            click_child(g, obj, k);
            g->btn_release = 0;
        }
    }
    if (g->press_part != PART_NONE)
        g->dirty = 1;
    g->press_part = PART_NONE;
}

void game_motion(Game *g, int x, int y)
{
    g->mouse_x = x;
    g->mouse_y = y;
    if (g->btn_obj >= 0) {
        int obj = g->btn_obj;
        int in = g->npopups && g->popups[g->npopups - 1] == obj && hotspot_at(g, obj, x, y) == g->btn_k;
        if (in != g->btn_in) {
            g->btn_in = in;
            g->dirty = 1;
        }
    }
    if (g->press_part == PART_THUMB && g->npopups && g->press_sb < g->nsb) {   /* drag: thumb centred on the pointer */
        DkObject *o = object(g, g->popups[g->npopups - 1]);
        Rect pr = popup_rect(g, o);
        ScrollBar *b = &g->sb[g->press_sb];
        Rect r = sb_rect(o, b, pr.x, pr.y + STRIP_H);
        int travel = r.h - 47;
        if (travel > 0 && b->range > 0)
            sb_set(g, o, b, ((y - 8 - r.y - 16) * b->range * 2 / travel + 1) >> 1);
        return;
    }
    int cur = CUR_NORMAL, obj = -1, k = -1;
    if (g->npopups) {
        int top = g->popups[g->npopups - 1];
        DkObject *o = object(g, top);
        if (o && in_rect(popup_rect(g, o), x, y))
            obj = top, k = hotspot_at(g, top, x, y);
    }
    if (obj < 0 && nav_button_at(g, x, y) >= 0)
        cur = CUR_HAND;                                  /* the nav buttons' cursor is Hand */
    else if (obj < 0 && in_rect(g->page_rect, x, y))
        obj = g->page, k = hotspot_at(g, g->page, x, y);
    if (k >= 0) {
        size_t n;
        const char *name = dkmm_string(&g->dk, object(g, obj)->children[k].cursor, &n);
        for (int i = 0; name && i < 5; i++)
            if (n == strlen(cursor_names[i]) && !SDL_strncasecmp(name, cursor_names[i], n))
                cur = i;
    }
    g->hover_cursor = cur;
}

int game_text_wanted(const Game *g)
{
    return g->npopups && g->widget_obj == g->popups[g->npopups - 1] && g->idx_edit >= 0;
}

/* Deletes the edit selection; returns 1 if there was one. */
static int idx_cut(Game *g)
{
    int a = g->idx_anchor < g->idx_caret ? g->idx_anchor : g->idx_caret;
    int b = g->idx_anchor < g->idx_caret ? g->idx_caret : g->idx_anchor;
    if (b <= a)
        return 0;
    memmove(g->idx_text + a, g->idx_text + b, strlen(g->idx_text + b) + 1);
    g->idx_anchor = g->idx_caret = a;
    return 1;
}

/* Typing (the edit box, FUN_1050_397a): replaces the selection, inserts at the caret. */
void game_text(Game *g, const char *utf8)
{
    if (!game_text_wanted(g))
        return;
    char add[64];
    utf8_to_cp1250(utf8, add, sizeof add);
    idx_cut(g);
    size_t n = strlen(g->idx_text), k = strlen(add);
    if (n + k >= sizeof g->idx_text)
        return;
    memmove(g->idx_text + g->idx_caret + k, g->idx_text + g->idx_caret, n - (size_t)g->idx_caret + 1);
    memcpy(g->idx_text + g->idx_caret, add, k);
    g->idx_caret += (int)k;
    g->idx_anchor = g->idx_caret;
    idx_search(g, object(g, g->widget_obj));
}

/* Index keys. The edit box (FUN_1050_397a, FUN_1050_2fc0): Backspace/Delete remove the selection or
 * one character; Left/Right/Home/End move the caret (Ctrl+Left/Right = Home/End), Shift extends the
 * selection. The list controller (FUN_1050_91ce) gets the same keys: Enter = OK, Up/Down one entry,
 * Page Up/Down one page of visible rows, Home/End the first/last entry. */
static int index_key(Game *g, SDL_Keycode key, SDL_Keymod mod)
{
    DkObject *o = object(g, g->widget_obj);
    int n = (int)strlen(g->idx_text), rows = idx_rows(g, o);
    int shift = (mod & SDL_KMOD_SHIFT) != 0, ctrl = (mod & SDL_KMOD_CTRL) != 0;
    int caret = -1;
    switch (key) {
    case SDLK_BACKSPACE:
    case SDLK_DELETE:
        if (!idx_cut(g)) {
            int at = key == SDLK_BACKSPACE ? g->idx_caret - 1 : g->idx_caret;
            if (at < 0 || at >= n)
                return 1;
            memmove(g->idx_text + at, g->idx_text + at + 1, (size_t)(n - at));
            g->idx_anchor = g->idx_caret = at;
        }
        idx_search(g, o);
        return 1;
    case SDLK_RETURN: case SDLK_KP_ENTER: idx_ok(g); return 1;
    case SDLK_DOWN: idx_select(g, o, g->idx_sel + 1); return 1;
    case SDLK_UP: idx_select(g, o, g->idx_sel < 0 ? 0 : g->idx_sel - 1); return 1;
    case SDLK_PAGEDOWN: idx_select(g, o, g->idx_sel + rows); return 1;
    case SDLK_PAGEUP: idx_select(g, o, g->idx_sel - rows); return 1;
    case SDLK_LEFT: caret = ctrl ? 0 : g->idx_caret - 1; break;
    case SDLK_RIGHT: caret = ctrl ? n : g->idx_caret + 1; break;
    case SDLK_HOME: idx_select(g, o, 0); n = (int)strlen(g->idx_text); caret = 0; break;
    case SDLK_END: idx_select(g, o, idx_count(g) - 1); n = (int)strlen(g->idx_text); caret = n; break;
    default: return 0;
    }
    g->idx_caret = caret < 0 ? 0 : caret > n ? n : caret;
    if (!shift)
        g->idx_anchor = g->idx_caret;
    g->dirty = 1;
    return 1;
}

void game_key(Game *g, SDL_Keycode key, SDL_Keymod mod)
{
    if (!g->media.kind && game_text_wanted(g) && index_key(g, key, mod))
        return;
    if (key == SDLK_BACKSPACE || key == SDLK_ESCAPE) {
        if (g->media.kind)
            media_stop(g);
        else if (key == SDLK_ESCAPE && g->npopups) {
            g->npopups--;
            g->dirty = 1;
        }
        else if (key == SDLK_BACKSPACE)
            go_back(g);
    }
}

SDL_Cursor *game_cursor(Game *g) { return g->cursor[g->hover_cursor]; }
int game_page(const Game *g) { return g->page; }
int game_popup(const Game *g) { return g->npopups ? g->popups[g->npopups - 1] : -1; }

/* ------------------------------------------------------------------ update */

void game_update(Game *g)
{
    DkObject *dial = object(g, g->page);
    if (dial && dial->ntable && !g->dial_pressed && SDL_GetTicksNS() >= g->dial_next_ns) {
        g->dial_lamp = g->dial_phase;          /* FUN_1048_e302: 172 when the phase is 1, else 173 */
        g->dial_phase ^= 1;
        g->dial_next_ns += 1000000000ull;
        if (!g->npopups)
            draw_dial_lamp(g);                 /* repaint only the lamp, like the original */
        else
            g->dirty = 1;
    }
    if (g->press_part != PART_NONE && g->press_part != PART_THUMB) {   /* held: repeat while on the part */
        Uint64 now = SDL_GetTicksNS();
        if (now >= g->press_next_ns && g->npopups && g->press_sb < g->nsb) {
            DkObject *o = object(g, g->popups[g->npopups - 1]);
            Rect pr = popup_rect(g, o);
            ScrollBar *b = &g->sb[g->press_sb];
            if (sb_part(b, sb_rect(o, b, pr.x, pr.y + STRIP_H), g->mouse_x, g->mouse_y) == g->press_part)
                sb_step(g, o, b, g->press_part);
            g->press_next_ns = now + SB_TICK_NS;
        }
    }
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
    g->btn_obj = -1;
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
    g->widget_obj = -1;
    g->idx_edit = g->idx_list = g->idx_sb = -1;
    /* Index text: the original's style 0x0BE3 colours (white, black, yellow, black) */
    g->col_bg = nearest_index(screen, 255, 255, 255);
    g->col_fg = nearest_index(screen, 0, 0, 0);
    g->col_sel_bg = nearest_index(screen, 255, 255, 0);
    g->col_sel_fg = g->col_fg;
    for (int i = 0; i < g->dk.nstyles; i++)
        if (g->dk.styles[i].name == 0x0be3) {
            uint8_t (*c)[3] = g->dk.styles[i].colours;
            g->col_bg = nearest_index(screen, c[0][0], c[0][1], c[0][2]);
            g->col_fg = nearest_index(screen, c[1][0], c[1][1], c[1][2]);
            g->col_sel_bg = nearest_index(screen, c[2][0], c[2][1], c[2][2]);
            g->col_sel_fg = nearest_index(screen, c[3][0], c[3][1], c[3][2]);
        }
    g->font = font_open(IDX_ROW_H);

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
        bmp = ne_bitmap_bmp(&g->ne, 173, NULL, &bl);
        if (bmp && dib_decode(bmp, bl, &g->exe_ok_dark, derr, sizeof derr) == 0) {
            remap_to_screen(screen, &g->exe_ok_dark);
            g->have_ok_dark = 1;
        }
        free(bmp);
        bmp = ne_bitmap_bmp(&g->ne, 352, NULL, &bl);
        if (bmp && dib_decode(bmp, bl, &g->exe_ok_pressed, derr, sizeof derr) == 0) {
            remap_to_screen(screen, &g->exe_ok_pressed);
            g->have_ok_pressed = 1;
        }
        free(bmp);
        static const int ids[3] = {157, 157, 107};        /* Normal, Arrow, Hand */
        for (int i = 0; i < 3; i++) {
            NeCursor c;
            if (ne_cursor(&g->ne, ids[i], &c) != 0)
                continue;
            SDL_Surface *s = SDL_CreateSurfaceFrom(c.width, c.height, SDL_PIXELFORMAT_ARGB8888, c.rgba, c.width * 4);
            if (s) {
                g->cursor[i] = SDL_CreateColorCursor(s, c.hot_x, c.hot_y);
                SDL_DestroySurface(s);
            }
            ne_cursor_free(&c);
        }
    }
    g->cursor[CUR_IBEAM] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
    g->cursor[CUR_WATCH] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_WAIT);
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
    for (int i = 0; i < 5; i++)
        if (g->cursor[i])
            SDL_DestroyCursor(g->cursor[i]);
    if (g->have_ok_lit)
        dib_free(&g->exe_ok_lit);
    if (g->have_ok_dark)
        dib_free(&g->exe_ok_dark);
    if (g->have_ok_pressed)
        dib_free(&g->exe_ok_pressed);
    font_close(g->font);
    for (int i = 0; i < g->nexe_bmp; i++)
        if (g->exe_bmp[i].ok)
            dib_free(&g->exe_bmp[i].dib);
    dkmm_close(&g->dk);
    free(g->png);
    free(g->exe);
    free(g);
}
