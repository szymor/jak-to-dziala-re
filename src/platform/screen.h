/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The 640x480 8-bit indexed screen with one global palette, shown through an SDL texture. */
#ifndef TWTW_SCREEN_H
#define TWTW_SCREEN_H

#include <SDL3/SDL.h>
#include <stdint.h>

#include "formats/dib.h"

#define SCREEN_W 640
#define SCREEN_H 480
/* The original's working area: its main window is sized (FUN_1010_24f0) to the page file's
 * 640x480 screen with a Windows 3.1 caption and thin border, leaving a 638x459 client area. The
 * layout templates fill exactly that (nav 94x459, page frames up to x 638 and y 459). The game
 * draws into the 640x480 buffer at the original's coordinates and shows its top-left 638x459. */
#define VIEW_W 638
#define VIEW_H 459

typedef struct Screen {
    uint8_t pixels[SCREEN_W * SCREEN_H];
    Rgb palette[256];
    SDL_Texture *texture;
    int dirty;
} Screen;

int screen_init(Screen *s, SDL_Renderer *r);
void screen_destroy(Screen *s);
void screen_clear(Screen *s, uint8_t index);
void screen_set_palette(Screen *s, const Rgb *pal, int n);
/* Copies a decoded bitmap to (x, y), clipped. */
void screen_blit(Screen *s, const Dib *d, int x, int y);
/* Uploads if dirty and draws the top-left part of the screen that matches the renderer's logical
 * size (VIEW_W x VIEW_H for the game, the whole buffer when the logical size is 640x480). */
void screen_present(Screen *s, SDL_Renderer *r);

#endif
