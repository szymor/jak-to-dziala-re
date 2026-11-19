/* Bitmap and animation viewer (twtw --viewer DATA [NAME]).
 *
 * DATA is the CD image (.iso) or a directory with the extracted CD. NAME
 * optionally selects the first item (e.g. ARTICLE1/AIRP/AIRP0M.DIB, AIRP0M1A).
 * Bitmaps and animations (.DIB, .ANI) are listed together; an animation plays
 * with its sound over its page bitmap (AIRP0M1A over AIRP0M).
 * Keys: Left/Right previous/next, PageUp/PageDown -/+20, Home/End, Space replay,
 * F11 fullscreen, Esc quit.
 */
#include <SDL3/SDL.h>

#include "viewer.h"
#include "engine/ani_player.h"
#include "formats/dib.h"
#include "platform/screen.h"
#include "vfs/vfs.h"

#include <stdlib.h>
#include <string.h>

typedef struct App {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_AudioDeviceID audio;
    Screen *screen_ptr;
    Screen screen;
    Vfs *vfs;
    const char **names;          /* all .DIB and .ANI paths (owned by the VFS) */
    size_t count, current;
    AniPlayer player;
    Rgb global_palette[256];
    char title[256];
} App;

static int ends_with(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && SDL_strcasecmp(s + a - b, suffix) == 0;
}

static void show_error(const char *title, const char *msg)
{
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s: %s", title, msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, msg, NULL);
}

static int load_dib(App *app, const char *name, Dib *out)
{
    size_t size;
    uint8_t *buf = vfs_read(app->vfs, name, &size);
    char err[128] = "not found";
    int rc = buf ? dib_decode(buf, size, out, err, sizeof err) : -1;
    if (rc != 0)
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s: %s", name, err);
    free(buf);
    return rc;
}

/* Where the game shows a bitmap: pages in the page area, the nav template and
 * full-screen pictures at (0, 0), anything else centred. */
static void placement(const Dib *d, int *x, int *y)
{
    if (d->width == 540 && d->height == 450)
        *x = 94, *y = 4;
    else if (d->width >= 600)
        *x = 0, *y = 0;
    else
        *x = (SCREEN_W - d->width) / 2, *y = (SCREEN_H - d->height) / 2;
}

/* ARTICLE1/AIRP/AIRP0M1A.ANI -> ARTICLE1/AIRP/AIRP0M.DIB; NAV animations -> NAV/NAV.DIB. */
static void base_bitmap_for(const char *ani, char *out, size_t outlen)
{
    SDL_strlcpy(out, ani, outlen);
    char *dot = strrchr(out, '.');
    if (dot && dot - out >= 2) {
        dot[-2] = 0;
        SDL_strlcat(out, ".DIB", outlen);
    }
    if (!strncmp(ani, "NAV/", 4))
        SDL_strlcpy(out, "NAV/NAV.DIB", outlen);
}

static void set_title(App *app, const char *extra)
{
    char t[320];
    SDL_snprintf(t, sizeof t, "Jak to działa? - %s [%zu/%zu]%s", app->names[app->current], app->current + 1,
                 app->count, extra);
    if (strcmp(t, app->title) != 0) {
        SDL_strlcpy(app->title, t, sizeof app->title);
        SDL_SetWindowTitle(app->window, t);
    }
}

static void load_current(App *app)
{
    ani_player_stop(&app->player);
    const char *name = app->names[app->current];
    screen_clear(&app->screen, 0);
    screen_set_palette(&app->screen, app->global_palette, 256);
    Dib dib;
    if (ends_with(name, ".DIB")) {
        if (load_dib(app, name, &dib) == 0) {
            int x, y;
            placement(&dib, &x, &y);
            screen_set_palette(&app->screen, dib.palette, dib.colours);
            screen_blit(&app->screen, &dib, x, y);
            char extra[64];
            SDL_snprintf(extra, sizeof extra, " %dx%d", dib.width, dib.height);
            set_title(app, extra);
            dib_free(&dib);
        }
        return;
    }
    /* Animation: draw its page bitmap first, then play on top at the same origin. */
    char base[512];
    int ox = 0, oy = 0;
    base_bitmap_for(name, base, sizeof base);
    if (vfs_exists(app->vfs, base) && load_dib(app, base, &dib) == 0) {
        placement(&dib, &ox, &oy);
        screen_blit(&app->screen, &dib, ox, oy);
        dib_free(&dib);
    }
    size_t size;
    uint8_t *buf = vfs_read(app->vfs, name, &size);
    char err[128] = "read error";
    if (!buf || ani_player_start(&app->player, buf, size, app->audio, ox, oy, err, sizeof err) != 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s: %s", name, err);
        set_title(app, " (cannot play)");
    }
}

static size_t find_start(App *app, const char *want)
{
    char norm[512];
    vfs_normalise(want, norm, sizeof norm);
    for (size_t i = 0; i < app->count; i++) {
        const char *n = app->names[i];
        const char *b = strrchr(n, '/');
        b = b ? b + 1 : n;
        size_t bl = strcspn(b, ".");
        if (!strcmp(n, norm) || (strlen(norm) == bl && !strncmp(b, norm, bl)))
            return i;
    }
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s not found, starting at the first item", want);
    return 0;
}

void *viewer_create(SDL_Window *window, SDL_Renderer *renderer, SDL_AudioDeviceID audio, Vfs *vfs, const char *start)
{
    App *app = SDL_calloc(1, sizeof *app);
    if (!app)
        return NULL;
    app->window = window;
    app->renderer = renderer;
    app->audio = audio;
    app->vfs = vfs;
    app->names = SDL_calloc(vfs_count(vfs), sizeof *app->names);
    for (size_t i = 0; i < vfs_count(vfs); i++) {
        const char *n = vfs_name(vfs, i);
        if (ends_with(n, ".DIB") || ends_with(n, ".ANI"))
            app->names[app->count++] = n;
    }
    if (!app->count || screen_init(&app->screen, renderer) != 0) {
        show_error("Viewer", "No bitmaps found - is this the right CD?");
        SDL_free(app->names);
        SDL_free(app);
        return NULL;
    }
    Dib pal;
    if (load_dib(app, "TITLE/TITL0M.DIB", &pal) == 0) {
        memcpy(app->global_palette, pal.palette, sizeof app->global_palette);
        dib_free(&pal);
    }
    app->current = start ? find_start(app, start) : 0;
    load_current(app);
    return app;
}

static void step(App *app, long delta)
{
    long n = (long)app->count, i = (long)app->current + delta;
    if (i < 0)
        i = 0;
    if (i >= n)
        i = n - 1;
    if ((size_t)i != app->current) {
        app->current = (size_t)i;
        load_current(app);
    }
}

int viewer_event(void *state, const SDL_Event *event)
{
    App *app = state;
    if (event->type == SDL_EVENT_KEY_DOWN) {
        switch (event->key.key) {
        case SDLK_ESCAPE: return 0;
        case SDLK_RIGHT: step(app, 1); break;
        case SDLK_LEFT: step(app, -1); break;
        case SDLK_PAGEDOWN: step(app, 20); break;
        case SDLK_PAGEUP: step(app, -20); break;
        case SDLK_HOME: step(app, -(long)app->count); break;
        case SDLK_END: step(app, (long)app->count); break;
        case SDLK_SPACE: load_current(app); break;
        default: break;
        }
    }
    return 1;
}

void viewer_iterate(void *state)
{
    App *app = state;
    if (app->player.file) {
        int playing = ani_player_update(&app->player, &app->screen);
        char extra[96];
        SDL_snprintf(extra, sizeof extra, " frame %d/%d %.1fs%s", app->player.next_frame, app->player.ani.frames,
                     ani_player_time(&app->player), playing ? "" : " (done)");
        set_title(app, extra);
    }
    screen_present(&app->screen, app->renderer);
}

void viewer_destroy(void *state)
{
    App *app = state;
    if (!app)
        return;
    ani_player_stop(&app->player);
    screen_destroy(&app->screen);
    SDL_free(app->names);
    SDL_free(app);
}
