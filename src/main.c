/* twtw: milestone 2 - bitmap and animation viewer over the original CD data.
 *
 *   twtw DATA [NAME]
 *
 * DATA is the CD image (.iso) or a directory with the extracted CD. NAME
 * optionally selects the first item (e.g. ARTICLE1/AIRP/AIRP0M.DIB, AIRP0M1A).
 * Bitmaps and animations (.DIB, .ANI) are listed together; an animation plays
 * with its sound over its page bitmap (AIRP0M1A over AIRP0M).
 * Keys: Left/Right previous/next, PageUp/PageDown -/+20, Home/End, Space replay,
 * F11 fullscreen, Esc quit.
 */
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

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

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv)
{
    SDL_SetAppMetadata("Jak to działa?", "0.2", "org.twtw.port");
    if (argc < 2) {
        show_error("Jak to działa?", "Usage: twtw DATA [NAME]\nDATA is the CD image (.iso) or an extracted CD directory.");
        return SDL_APP_FAILURE;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        show_error("SDL_Init", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    App *app = SDL_calloc(1, sizeof *app);
    if (!app)
        return SDL_APP_FAILURE;
    *appstate = app;

    char err[256];
    app->vfs = vfs_open(argv[1], err, sizeof err);
    if (!app->vfs) {
        show_error("Cannot open game data", err);
        return SDL_APP_FAILURE;
    }
    app->names = SDL_calloc(vfs_count(app->vfs), sizeof *app->names);
    for (size_t i = 0; i < vfs_count(app->vfs); i++) {
        const char *n = vfs_name(app->vfs, i);
        if (ends_with(n, ".DIB") || ends_with(n, ".ANI"))
            app->names[app->count++] = n;
    }
    if (!app->count) {
        show_error("Cannot open game data", "No bitmaps found - is this the right CD?");
        return SDL_APP_FAILURE;
    }
    /* All game bitmaps share one palette; take it from the title screen. */
    Dib pal;
    if (load_dib(app, "TITLE/TITL0M.DIB", &pal) == 0) {
        memcpy(app->global_palette, pal.palette, sizeof app->global_palette);
        dib_free(&pal);
    }

    if (!SDL_CreateWindowAndRenderer("Jak to działa?", SCREEN_W * 2, SCREEN_H * 2, SDL_WINDOW_RESIZABLE,
                                     &app->window, &app->renderer)) {
        show_error("SDL_CreateWindowAndRenderer", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderLogicalPresentation(app->renderer, SCREEN_W, SCREEN_H, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderVSync(app->renderer, 1);
    if (screen_init(&app->screen, app->renderer) != 0) {
        show_error("SDL_CreateTexture", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO))
        app->audio = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
    if (!app->audio)
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "no audio device (%s); animations play silently", SDL_GetError());

    app->current = argc >= 3 ? find_start(app, argv[2]) : 0;
    load_current(app);
    return SDL_APP_CONTINUE;
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

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    App *app = appstate;
    if (event->type == SDL_EVENT_QUIT)
        return SDL_APP_SUCCESS;
    if (event->type == SDL_EVENT_KEY_DOWN) {
        switch (event->key.key) {
        case SDLK_ESCAPE: return SDL_APP_SUCCESS;
        case SDLK_RIGHT: step(app, 1); break;
        case SDLK_LEFT: step(app, -1); break;
        case SDLK_PAGEDOWN: step(app, 20); break;
        case SDLK_PAGEUP: step(app, -20); break;
        case SDLK_HOME: step(app, -(long)app->count); break;
        case SDLK_END: step(app, (long)app->count); break;
        case SDLK_SPACE: load_current(app); break;
        case SDLK_F11:
            SDL_SetWindowFullscreen(app->window, !(SDL_GetWindowFlags(app->window) & SDL_WINDOW_FULLSCREEN));
            break;
        default: break;
        }
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    App *app = appstate;
    if (app->player.file) {
        int playing = ani_player_update(&app->player, &app->screen);
        char extra[96];
        SDL_snprintf(extra, sizeof extra, " frame %d/%d %.1fs%s", app->player.next_frame, app->player.ani.frames,
                     ani_player_time(&app->player), playing ? "" : " (done)");
        set_title(app, extra);
    }
    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 255);
    SDL_RenderClear(app->renderer);
    screen_present(&app->screen, app->renderer);
    SDL_RenderPresent(app->renderer);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    App *app = appstate;
    if (!app)
        return;
    ani_player_stop(&app->player);
    if (app->audio)
        SDL_CloseAudioDevice(app->audio);
    screen_destroy(&app->screen);
    SDL_free(app->names);
    vfs_close(app->vfs);
    SDL_free(app);
}
