/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Resources of a Win16 NE executable (TWTW.EXE): the UI bitmaps and the hand cursor
 * are loaded from the user's own copy at run time. See docs/formats/exe_resources.md. */
#ifndef TWTW_NE_RES_H
#define TWTW_NE_RES_H

#include <stddef.h>
#include <stdint.h>

enum { NE_RT_CURSOR = 1, NE_RT_BITMAP = 2, NE_RT_ICON = 3, NE_RT_GROUP_CURSOR = 12 };

typedef struct NeRes {
    const uint8_t *data;         /* not owned */
    size_t size;
    size_t rt;                   /* resource table offset */
    int shift;
} NeRes;

typedef struct NeCursor {
    int width, height, hot_x, hot_y;
    uint32_t *rgba;              /* width*height ARGB8888 (malloc'd) */
} NeCursor;

int ne_open(NeRes *ne, const uint8_t *data, size_t size, char *err, size_t errlen);

/* Finds a resource by numeric id (name == NULL) or by name (case-insensitive).
 * Returns a pointer into the EXE data and its length, or NULL. */
const uint8_t *ne_find(const NeRes *ne, int type, int id, const char *name, size_t *len);

/* Calls cb for every resource; name is NULL for numeric ids. */
typedef void (*NeEnumFn)(void *user, int type, int id, const char *name, const uint8_t *p, size_t len);
void ne_enum(const NeRes *ne, NeEnumFn cb, void *user);

/* A BITMAP resource wrapped as a complete .bmp file (malloc'd), ready for dib_decode. */
uint8_t *ne_bitmap_bmp(const NeRes *ne, int id, const char *name, size_t *len);

/* A cursor from a GROUP_CURSOR id, converted to ARGB (transparent where the AND mask is set). */
int ne_cursor(const NeRes *ne, int group_id, NeCursor *out);
void ne_cursor_free(NeCursor *c);

#endif
