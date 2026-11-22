/* The game engine: pages, pop-ups, hotspots, actions and media, driven by
 * TWTW.PNG (docs/formats/dkmm.md) and the behaviour verified in the original. */
#ifndef TWTW_GAME_H
#define TWTW_GAME_H

#include <SDL3/SDL.h>

#include "platform/screen.h"
#include "vfs/vfs.h"

typedef struct Game Game;

Game *game_create(Vfs *vfs, Screen *screen, SDL_AudioDeviceID audio, char *err, size_t errlen);
void game_destroy(Game *g);

/* Opens the start page (the title) or, for testing, any object index. */
void game_start(Game *g, int object);

/* Input in screen coordinates (640x480). */
void game_click(Game *g, int x, int y);
void game_motion(Game *g, int x, int y);
void game_mouse_up(Game *g);
/* Text typed by the user (UTF-8), for the Index search field. */
void game_text(Game *g, const char *utf8);
/* True while a text field has the focus (the app should enable SDL text input). */
int game_text_wanted(const Game *g);
void game_key(Game *g, SDL_Keycode key, SDL_Keymod mod);

/* Advances media and timers; composes the screen when needed. */
void game_update(Game *g);

/* Cursor that should be shown now (NULL = system default). */
SDL_Cursor *game_cursor(Game *g);

/* Checks every object's references and opens every page/pop-up; prints problems, returns their count. */
int game_selftest(Game *g);

/* Current page object index (for tests/titles). */
int game_page(const Game *g);
int game_popup(const Game *g);   /* top pop-up or -1 */

#endif
