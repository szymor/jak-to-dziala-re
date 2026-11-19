/* twtw: milestone 1 - bitmap viewer over the original CD data.
 *
 *   twtw DATA [NAME]
 *
 * DATA is the CD image (.iso) or a directory with the extracted CD. NAME
 * optionally selects the first bitmap (e.g. ARTICLE1/AIRP/AIRP0M.DIB or just AIRP0M).
 * Keys: Left/Right previous/next, PageUp/PageDown -/+20, Home/End, F11 fullscreen, Esc quit.
 */
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "formats/dib.h"
#include "vfs/vfs.h"

#include <stdlib.h>
#include <string.h>

#define SCREEN_W 640
#define SCREEN_H 480

typedef struct App {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    Vfs *vfs;
    char **names;          /* all .DIB paths */
    size_t count, current;
    int shown_w, shown_h;
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

static void load_current(App *app)
{
    const char *name = app->names[app->current];
    size_t size;
    uint8_t *buf = vfs_read(app->vfs, name, &size);
    Dib dib;
    char err[128] = "read error";
    if (!buf || dib_decode(buf, size, &dib, err, sizeof err) != 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s: %s", name, err);
        free(buf);
        return;
    }
    free(buf);

    /* Indexed pixels -> RGBA via the bitmap's own palette (all game bitmaps share one). */
    uint32_t *rgba = malloc((size_t)dib.width * dib.height * 4);
    if (rgba) {
        for (size_t i = 0, n = (size_t)dib.width * dib.height; i < n; i++) {
            Rgb c = dib.palette[dib.pixels[i]];
            rgba[i] = 0xFF000000u | ((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | c.b;
        }
        SDL_DestroyTexture(app->texture);
        app->texture = SDL_CreateTexture(app->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC,
                                         dib.width, dib.height);
        if (app->texture) {
            SDL_SetTextureScaleMode(app->texture, SDL_SCALEMODE_NEAREST);
            SDL_UpdateTexture(app->texture, NULL, rgba, dib.width * 4);
        }
        free(rgba);
    }
    app->shown_w = dib.width;
    app->shown_h = dib.height;
    dib_free(&dib);

    char title[256];
    SDL_snprintf(title, sizeof title, "Jak to działa? - %s (%dx%d) [%zu/%zu]", name, app->shown_w, app->shown_h,
                 app->current + 1, app->count);
    SDL_SetWindowTitle(app->window, title);
}

static size_t find_start(App *app, const char *want)
{
    char norm[512];
    vfs_normalise(want, norm, sizeof norm);
    for (size_t i = 0; i < app->count; i++) {
        const char *n = app->names[i];
        const char *base = strrchr(n, '/');
        base = base ? base + 1 : n;
        size_t bl = strcspn(base, ".");
        if (!strcmp(n, norm) || (strlen(norm) == bl && !strncmp(base, norm, bl)))
            return i;
    }
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "bitmap %s not found, starting at the first one", want);
    return 0;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv)
{
    SDL_SetAppMetadata("Jak to działa?", "0.1", "org.twtw.port");
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
    for (size_t i = 0; i < vfs_count(app->vfs); i++)
        if (ends_with(vfs_name(app->vfs, i), ".DIB"))
            app->count++;
    if (!app->count) {
        show_error("Cannot open game data", "No .DIB bitmaps found - is this the right CD?");
        return SDL_APP_FAILURE;
    }
    app->names = SDL_calloc(app->count, sizeof *app->names);
    for (size_t i = 0, k = 0; i < vfs_count(app->vfs); i++)
        if (ends_with(vfs_name(app->vfs, i), ".DIB"))
            app->names[k++] = (char *)vfs_name(app->vfs, i);

    if (!SDL_CreateWindowAndRenderer("Jak to działa?", SCREEN_W * 2, SCREEN_H * 2, SDL_WINDOW_RESIZABLE,
                                     &app->window, &app->renderer)) {
        show_error("SDL_CreateWindowAndRenderer", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderLogicalPresentation(app->renderer, SCREEN_W, SCREEN_H, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderVSync(app->renderer, 1);

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
    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 255);
    SDL_RenderClear(app->renderer);
    if (app->texture) {
        /* Page-sized bitmaps sit in the page area like in the game; others are centred. */
        SDL_FRect dst = {(float)(SCREEN_W - app->shown_w) / 2, (float)(SCREEN_H - app->shown_h) / 2,
                         (float)app->shown_w, (float)app->shown_h};
        if (app->shown_w == 540 && app->shown_h == 450)
            dst.x = 94, dst.y = 4;
        SDL_RenderTexture(app->renderer, app->texture, NULL, &dst);
    }
    SDL_RenderPresent(app->renderer);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    App *app = appstate;
    if (!app)
        return;
    SDL_DestroyTexture(app->texture);
    SDL_free(app->names);
    vfs_close(app->vfs);
    SDL_free(app);
}
