/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Bitmap/animation viewer mode (twtw --viewer DATA [NAME]). */
#ifndef TWTW_VIEWER_H
#define TWTW_VIEWER_H

#include <SDL3/SDL.h>

#include "vfs/vfs.h"

void *viewer_create(SDL_Window *window, SDL_Renderer *renderer, SDL_AudioDeviceID audio, Vfs *vfs, const char *start);
int viewer_event(void *state, const SDL_Event *event);   /* 0 = quit */
void viewer_iterate(void *state);                        /* draws to the renderer */
void viewer_destroy(void *state);

#endif
