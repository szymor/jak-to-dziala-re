/* Windows DIB/BMP reader for the game's 8-bit bitmaps (RLE8 or uncompressed). */
#ifndef TWTW_DIB_H
#define TWTW_DIB_H

#include <stddef.h>
#include <stdint.h>

typedef struct Rgb {
    uint8_t r, g, b;
} Rgb;

typedef struct Dib {
    int width, height;
    uint8_t *pixels;   /* width*height palette indices, top-down rows, no padding */
    Rgb palette[256];
    int colours;       /* palette entries present in the file */
} Dib;

/* Decodes a BMP file held in memory. Returns 0 on success, -1 on error (msg in err). */
int dib_decode(const uint8_t *data, size_t size, Dib *out, char *err, size_t errlen);
void dib_free(Dib *dib);

#endif
