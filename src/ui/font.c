#include "ui/font.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <stdlib.h>
#include <string.h>

struct Font {
    TTF_Font *ttf;
    char path[512];
    int line;
};

static const char *const candidates[] = {
    /* the original's face (Windows, macOS, or user-installed) */
    "C:/Windows/Fonts/times.ttf",
    "/System/Library/Fonts/Supplemental/Times New Roman.ttf",
    "/Library/Fonts/Times New Roman.ttf",
    "/usr/share/fonts/TTF/times.ttf",
    "/usr/share/fonts/truetype/msttcorefonts/Times_New_Roman.ttf",
    /* metric-compatible free substitutes */
    "/usr/share/fonts/liberation/LiberationSerif-Regular.ttf",
    "/usr/share/fonts/TTF/LiberationSerif-Regular.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf",
    "/usr/share/fonts/liberation-serif/LiberationSerif-Regular.ttf",
    "/usr/share/fonts/truetype/croscore/Tinos-Regular.ttf",
    "/usr/share/fonts/TTF/Tinos-Regular.ttf",
    "/usr/share/fonts/OTF/texgyretermes-regular.otf",
    "/usr/share/texmf/fonts/opentype/public/tex-gyre/texgyretermes-regular.otf",
};

Font *font_open(int line_px)
{
    if (!TTF_WasInit() && !TTF_Init()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL_ttf: %s", SDL_GetError());
        return NULL;
    }
    const char *env = SDL_getenv("TWTW_FONT");
    size_t n = sizeof candidates / sizeof *candidates;
    for (size_t i = env ? 0 : 1; i <= n; i++) {
        const char *path = i == 0 ? env : candidates[i - 1];
        if (!path || !SDL_GetPathInfo(path, NULL))
            continue;
        /* find the point size whose line spacing matches the original's rows */
        TTF_Font *best = NULL;
        for (float pt = 8; pt <= 16; pt += 0.5f) {
            TTF_Font *f = TTF_OpenFont(path, pt);
            if (!f)
                break;
            if (TTF_GetFontLineSkip(f) > line_px) {
                TTF_CloseFont(f);
                break;
            }
            if (best)
                TTF_CloseFont(best);
            best = f;
        }
        if (!best)
            continue;
        Font *font = calloc(1, sizeof *font);
        font->ttf = best;
        font->line = line_px;
        SDL_strlcpy(font->path, path, sizeof font->path);
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "font %s (line %d)", path, TTF_GetFontLineSkip(best));
        return font;
    }
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "no serif font found for the Index screen; set TWTW_FONT to a .ttf (e.g. Times New Roman)");
    return NULL;
}

void font_close(Font *f)
{
    if (!f)
        return;
    TTF_CloseFont(f->ttf);
    free(f);
}

const char *font_path(const Font *f) { return f ? f->path : ""; }
int font_line(const Font *f) { return f ? f->line : 12; }
int font_ascent(const Font *f) { return f ? TTF_GetFontAscent(f->ttf) : 10; }

int font_width(Font *f, const char *utf8, size_t len)
{
    int w = 0, h = 0;
    if (f && len)
        TTF_GetStringSize(f->ttf, utf8, len, &w, &h);
    return w;
}

int font_draw(Font *f, Screen *s, const char *utf8, size_t len, int x, int y, Clip clip, uint8_t fg)
{
    if (!f || !len)
        return 0;
    SDL_Surface *t = TTF_RenderText_Solid(f->ttf, utf8, len, (SDL_Color){0, 0, 0, 255});
    if (!t)
        return 0;
    /* Solid rendering is 8-bit: index 0 background, non-zero foreground */
    for (int row = 0; row < t->h; row++) {
        int dy = y + row;
        if (dy < clip.y || dy >= clip.y + clip.h || dy < 0 || dy >= SCREEN_H)
            continue;
        const uint8_t *src = (const uint8_t *)t->pixels + (size_t)row * t->pitch;
        for (int col = 0; col < t->w; col++) {
            int dx = x + col;
            if (src[col] && dx >= clip.x && dx < clip.x + clip.w && dx >= 0 && dx < SCREEN_W)
                s->pixels[dy * SCREEN_W + dx] = fg;
        }
    }
    int w = t->w;
    SDL_DestroySurface(t);
    s->dirty = 1;
    return w;
}
