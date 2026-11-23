/* SPDX-License-Identifier: GPL-3.0-or-later */
/* twtw: the Jak to działa? player.
 *
 *   twtw [DATA] [--page N] [--fullscreen | --windowed] [--integer-scale | --smooth-scale]
 *   twtw --viewer DATA [NAME]     browse bitmaps and animations
 *
 * DATA is the CD image (.iso) or a directory with the extracted CD. Without DATA the last one
 * used is taken from the settings file; the first time, a file dialog asks for the CD image.
 * Display options given on the command line are remembered.
 *
 * The original has no keyboard shortcuts (its window procedures and main loop handle no keys;
 * only the Index screen's edit field and list take keys). Port additions: Backspace = the Wróć
 * button, Esc = stop sound/animation or close the top pop-up, F11 or Alt+Enter = fullscreen.
 */
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "engine/game.h"
#include "platform/screen.h"
#include "platform/settings.h"
#include "viewer.h"
#include "vfs/vfs.h"

#include <stdlib.h>
#include <string.h>

typedef struct App {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_AudioDeviceID audio;
    Vfs *vfs;
    Screen screen;
    Game *game;
    void *viewer;
    SDL_Cursor *cursor_shown;
    int cursor_set;
    int page;
    Settings settings;
    /* file dialog result (the callback may run on another thread) */
    SDL_AtomicInt dialog_state;         /* 0 none, 1 waiting, 2 chosen, 3 cancelled */
    char dialog_path[1024];
} App;

static void show_error(const char *title, const char *msg)
{
    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s: %s", title, msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, msg, NULL);
}

static void apply_display(App *app)
{
    SDL_SetRenderLogicalPresentation(app->renderer, SCREEN_W, SCREEN_H,
                                     app->settings.integer_scale ? SDL_LOGICAL_PRESENTATION_INTEGER_SCALE
                                                                 : SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetWindowFullscreen(app->window, app->settings.fullscreen);
}

/* Opens the data and starts the game; 0 on success. */
static int start_game(App *app, const char *data)
{
    char err[256];
    app->vfs = vfs_open(data, err, sizeof err);
    if (!app->vfs) {
        show_error("Cannot open game data", err);
        return -1;
    }
    if (screen_init(&app->screen, app->renderer) != 0) {
        show_error("SDL_CreateTexture", SDL_GetError());
        return -1;
    }
    app->game = game_create(app->vfs, &app->screen, app->audio, err, sizeof err);
    if (!app->game) {
        show_error("Cannot load the game", err);
        return -1;
    }
    SDL_strlcpy(app->settings.data, data, sizeof app->settings.data);   /* remember what worked */
    settings_save(&app->settings);
    game_start(app->game, app->page);
    return 0;
}

static void SDLCALL dialog_done(void *userdata, const char *const *files, int filter)
{
    (void)filter;
    App *app = userdata;
    if (files && files[0]) {
        SDL_strlcpy(app->dialog_path, files[0], sizeof app->dialog_path);
        SDL_SetAtomicInt(&app->dialog_state, 2);
    } else {
        SDL_SetAtomicInt(&app->dialog_state, 3);
    }
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv)
{
    SDL_SetAppMetadata("Jak to działa?", "0.6", "org.twtw.port");
    int viewer = 0, page = -1, fullscreen = -1, integer = -1;
    const char *data = NULL, *name = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--viewer"))
            viewer = 1;
        else if (!strcmp(argv[i], "--page") && i + 1 < argc)
            page = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fullscreen"))
            fullscreen = 1;
        else if (!strcmp(argv[i], "--windowed"))
            fullscreen = 0;
        else if (!strcmp(argv[i], "--integer-scale"))
            integer = 1;
        else if (!strcmp(argv[i], "--smooth-scale"))
            integer = 0;
        else if (!data)
            data = argv[i];
        else
            name = argv[i];
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        show_error("SDL_Init", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    App *app = SDL_calloc(1, sizeof *app);
    if (!app)
        return SDL_APP_FAILURE;
    *appstate = app;
    app->page = page;
    settings_load(&app->settings);
    if (fullscreen >= 0)
        app->settings.fullscreen = fullscreen;
    if (integer >= 0)
        app->settings.integer_scale = integer;

    if (!SDL_CreateWindowAndRenderer("Jak to działa?", SCREEN_W * 2, SCREEN_H * 2, SDL_WINDOW_RESIZABLE,
                                     &app->window, &app->renderer)) {
        show_error("SDL_CreateWindowAndRenderer", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderVSync(app->renderer, 1);
    if (SDL_InitSubSystem(SDL_INIT_AUDIO))
        app->audio = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
    if (!app->audio)
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "no audio device (%s); playing silently", SDL_GetError());

    if (viewer) {
        if (!data) {
            show_error("Jak to działa?", "Usage: twtw --viewer DATA [NAME]");
            return SDL_APP_FAILURE;
        }
        char err[256];
        app->vfs = vfs_open(data, err, sizeof err);
        if (!app->vfs) {
            show_error("Cannot open game data", err);
            return SDL_APP_FAILURE;
        }
        SDL_SetRenderLogicalPresentation(app->renderer, SCREEN_W, SCREEN_H, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        app->viewer = viewer_create(app->window, app->renderer, app->audio, app->vfs, name);
        return app->viewer ? SDL_APP_CONTINUE : SDL_APP_FAILURE;
    }
    apply_display(app);
    if (!data && app->settings.data[0])
        data = app->settings.data;
    if (data)
        return start_game(app, data) == 0 ? SDL_APP_CONTINUE : SDL_APP_FAILURE;

    /* first run: ask for the CD image */
    static const SDL_DialogFileFilter filters[] = {{"CD image (*.iso)", "iso"}, {"All files", "*"}};
    SDL_SetAtomicInt(&app->dialog_state, 1);
    SDL_ShowOpenFileDialog(dialog_done, app, app->window, filters, 2, NULL, false);
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    App *app = appstate;
    if (event->type == SDL_EVENT_QUIT)
        return SDL_APP_SUCCESS;
    if (event->type == SDL_EVENT_KEY_DOWN &&
        (event->key.key == SDLK_F11 || (event->key.key == SDLK_RETURN && (event->key.mod & SDL_KMOD_ALT)))) {
        app->settings.fullscreen = !(SDL_GetWindowFlags(app->window) & SDL_WINDOW_FULLSCREEN);
        SDL_SetWindowFullscreen(app->window, app->settings.fullscreen);
        if (!app->viewer)
            settings_save(&app->settings);
        return SDL_APP_CONTINUE;
    }
    if (app->viewer)
        return viewer_event(app->viewer, event) ? SDL_APP_CONTINUE : SDL_APP_SUCCESS;
    if (!app->game)
        return SDL_APP_CONTINUE;
    SDL_ConvertEventToRenderCoordinates(app->renderer, event);
    switch (event->type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event->button.button == SDL_BUTTON_LEFT)
            game_click(app->game, (int)event->button.x, (int)event->button.y);
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event->button.button == SDL_BUTTON_LEFT)
            game_mouse_up(app->game);
        break;
    case SDL_EVENT_MOUSE_MOTION:
        game_motion(app->game, (int)event->motion.x, (int)event->motion.y);
        break;
    case SDL_EVENT_TEXT_INPUT:
        game_text(app->game, event->text.text);
        break;
    case SDL_EVENT_KEY_DOWN:
        game_key(app->game, event->key.key, event->key.mod);
        break;
    default:
        break;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
    App *app = appstate;
    SDL_SetRenderDrawColor(app->renderer, 0, 0, 0, 255);
    SDL_RenderClear(app->renderer);
    if (app->viewer) {
        viewer_iterate(app->viewer);
    } else if (!app->game) {
        int st = SDL_GetAtomicInt(&app->dialog_state);
        if (st == 3)
            return SDL_APP_SUCCESS;              /* the dialog was cancelled */
        if (st == 2) {
            SDL_SetAtomicInt(&app->dialog_state, 0);
            if (start_game(app, app->dialog_path) != 0)
                return SDL_APP_FAILURE;
        }
    } else {
        game_update(app->game);
        screen_present(&app->screen, app->renderer);
        int want_text = game_text_wanted(app->game);
        if (want_text != SDL_TextInputActive(app->window)) {
            if (want_text)
                SDL_StartTextInput(app->window);
            else
                SDL_StopTextInput(app->window);
        }
        SDL_Cursor *want = game_cursor(app->game);
        if (!app->cursor_set || want != app->cursor_shown) {
            SDL_SetCursor(want ? want : SDL_GetDefaultCursor());
            app->cursor_shown = want;
            app->cursor_set = 1;
        }
    }
    SDL_RenderPresent(app->renderer);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    (void)result;
    App *app = appstate;
    if (!app)
        return;
    viewer_destroy(app->viewer);
    game_destroy(app->game);
    screen_destroy(&app->screen);
    if (app->audio)
        SDL_CloseAudioDevice(app->audio);
    vfs_close(app->vfs);
    SDL_free(app);
}
