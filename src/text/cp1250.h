/* CP1250 text helpers: the game's strings are Windows-1250. */
#ifndef TWTW_CP1250_H
#define TWTW_CP1250_H

#include <stddef.h>
#include <stdint.h>

/* Converts n CP1250 bytes to NUL-terminated UTF-8; returns the UTF-8 length. */
size_t cp1250_to_utf8(const char *src, size_t n, char *dst, size_t dstlen);
/* Converts NUL-terminated UTF-8 to CP1250, dropping characters CP1250 lacks. */
size_t utf8_to_cp1250(const char *src, char *dst, size_t dstlen);
/* Polish index order (see cp1250.c): <0, 0, >0. */
int cp1250_compare(const char *a, size_t na, const char *b, size_t nb);

#endif
