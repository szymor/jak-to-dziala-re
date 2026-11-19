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
void game_key(Game *g, SDL_Keycode key);

/* Advances media and timers; composes the screen when needed. */
void game_update(Game *g);

/* Cursor that should be shown now (NULL = system default). */
SDL_Cursor *game_cursor(Game *g);

/* Current page object index (for tests/titles). */
int game_page(const Game *g);
int game_popup(const Game *g);   /* top pop-up or -1 */

#endif
