/* Runtime text (only the Index screen draws text): the original used "Times New Roman CE";
 * we use it if the user has it, else a metric-compatible serif (Liberation Serif, Tinos,
 * TeX Gyre Termes). Text is drawn without antialiasing, like Windows 3.1. */
#ifndef TWTW_FONT_H
#define TWTW_FONT_H

#include <stdint.h>

#include "platform/screen.h"

typedef struct Font Font;

typedef struct Clip {
    int x, y, w, h;
} Clip;

/* Opens the first usable font, sized so lines are 'line_px' apart. NULL if none found. */
Font *font_open(int line_px);
void font_close(Font *f);
const char *font_path(const Font *f);
int font_line(const Font *f);
int font_ascent(const Font *f);
int font_width(Font *f, const char *utf8, size_t len);
/* Draws UTF-8 text with its top-left at (x, y) in palette colour fg, clipped. Returns the width. */
int font_draw(Font *f, Screen *s, const char *utf8, size_t len, int x, int y, Clip clip, uint8_t fg);

#endif
