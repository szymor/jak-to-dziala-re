/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The 640x480 8-bit indexed screen with one global palette, shown through an SDL texture. */
#ifndef TWTW_SCREEN_H
#define TWTW_SCREEN_H

#include <SDL3/SDL.h>
#include <stdint.h>

#include "formats/dib.h"

#define SCREEN_W 640
#define SCREEN_H 480

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
/* Uploads if dirty and draws the screen to the renderer's logical 640x480 area. */
void screen_present(Screen *s, SDL_Renderer *r);

#endif
