/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "platform/screen.h"

#include <string.h>

int screen_init(Screen *s, SDL_Renderer *r)
{
    memset(s, 0, sizeof *s);
    s->texture = SDL_CreateTexture(r, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_W, SCREEN_H);
    if (!s->texture)
        return -1;
    SDL_SetTextureScaleMode(s->texture, SDL_SCALEMODE_NEAREST);
    s->dirty = 1;
    return 0;
}

void screen_destroy(Screen *s)
{
    SDL_DestroyTexture(s->texture);
    s->texture = NULL;
}

void screen_clear(Screen *s, uint8_t index)
{
    memset(s->pixels, index, sizeof s->pixels);
    s->dirty = 1;
}

void screen_set_palette(Screen *s, const Rgb *pal, int n)
{
    memcpy(s->palette, pal, (size_t)(n > 256 ? 256 : n) * sizeof *pal);
    s->dirty = 1;
}

void screen_blit(Screen *s, const Dib *d, int x, int y)
{
    for (int row = 0; row < d->height; row++) {
        int dy = y + row;
        if (dy < 0 || dy >= SCREEN_H)
            continue;
        int sx = 0, dx = x, n = d->width;
        if (dx < 0) {
            sx = -dx;
            n -= sx;
            dx = 0;
        }
        if (dx + n > SCREEN_W)
            n = SCREEN_W - dx;
        if (n > 0)
            memcpy(s->pixels + (size_t)dy * SCREEN_W + dx, d->pixels + (size_t)row * d->width + sx, (size_t)n);
    }
    s->dirty = 1;
}

void screen_present(Screen *s, SDL_Renderer *r)
{
    if (s->dirty) {
        uint32_t lut[256];
        for (int i = 0; i < 256; i++)
            lut[i] = 0xFF000000u | ((uint32_t)s->palette[i].r << 16) | ((uint32_t)s->palette[i].g << 8) | s->palette[i].b;
        void *pixels;
        int pitch;
        if (SDL_LockTexture(s->texture, NULL, &pixels, &pitch)) {
            for (int y = 0; y < SCREEN_H; y++) {
                uint32_t *dst = (uint32_t *)((uint8_t *)pixels + (size_t)y * pitch);
                const uint8_t *src = s->pixels + (size_t)y * SCREEN_W;
                for (int x = 0; x < SCREEN_W; x++)
                    dst[x] = lut[src[x]];
            }
            SDL_UnlockTexture(s->texture);
        }
        s->dirty = 0;
    }
    int w = SCREEN_W, h = SCREEN_H;
    SDL_RendererLogicalPresentation mode;
    if (SDL_GetRenderLogicalPresentation(r, &w, &h, &mode) && (w <= 0 || h <= 0 || w > SCREEN_W || h > SCREEN_H))
        w = SCREEN_W, h = SCREEN_H;
    SDL_FRect src = {0, 0, (float)w, (float)h};
    SDL_RenderTexture(r, s->texture, &src, NULL);
}
