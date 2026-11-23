/* SPDX-License-Identifier: GPL-3.0-or-later */
/* TWTW.PNG: the compiled DKMM page file (see docs/formats/dkmm.md).
 *
 * dkmm_open() parses the header, string pool, template section, index table
 * and object table. Object records are parsed on demand with dkmm_object().
 * String references are 1-based (0 = none); object references are 0-based
 * indices into the object table.
 */
#ifndef TWTW_DKMM_H
#define TWTW_DKMM_H

#include <stddef.h>
#include <stdint.h>

typedef struct DkAction {
    uint16_t type;
    uint16_t args[10];
    int nargs;
} DkAction;

typedef struct DkEvent {
    uint16_t event;              /* 1 on open, 4 click, 6 animation finished */
    int nactions;
    DkAction *actions;
} DkEvent;

enum {
    DK_CHILD_BITMAP = 1,         /* class 3 sub 1 */
    DK_CHILD_HOTSPOT,            /* classes 2, 3, 4, 5, 7, 12, 13, 15, 16 */
    DK_CHILD_NAVBUTTON,          /* class 6 */
    DK_CHILD_CONTENT,            /* class 9 */
    DK_CHILD_SCROLLBAR,          /* class 10 */
    DK_CHILD_LISTCTL             /* class 14 */
};

typedef struct DkChild {
    int kind;
    uint16_t cls, sub;
    /* hotspot body (kind HOTSPOT, NAVBUTTON) */
    uint16_t id, shape, rect[4], cursor, flags[3];
    int nevents;
    DkEvent *events;
    uint16_t tail[8];            /* class-specific tail words after the hotspot terminator */
    int ntail;
    uint16_t file;               /* bitmap / content / class-2 animation / class-6,7 media */
    uint16_t anim_rect[4], anim_cursor, anim_params[9];   /* class 2 */
    uint16_t images[2];          /* classes 15, 16 */
    uint16_t style;              /* classes 12, 13 */
    uint8_t fg[3], bg[3], lbflag;   /* class 13 */
    char source[32], face[32];   /* class 13 */
    uint16_t raw[29];            /* fixed-size classes 6, 9, 10, 14 */
    int nraw;
} DkChild;

typedef struct DkObject {
    int index;
    uint16_t type;               /* 0 template, 1 pop-up, 2 page, 3 index screen */
    uint16_t tmpl;               /* type 0: name id */
    uint16_t x, y, dir, w5, panel[3];
    uint16_t nchildren, id, width, height;
    int nchild;
    DkChild *children;
    int nevents;                 /* object-level events (trailer) */
    DkEvent *events;
    int nlinks;
    uint32_t (*links)[4];        /* types 1-3: 4 context links per entry */
    uint32_t (*tlinks)[2];       /* type 0: (button id, target object) */
    int ntable;                  /* A-Z dial table */
    uint32_t *table_targets;
    uint16_t *table_anims;
    uint16_t table_extra;
} DkObject;

typedef struct DkPanel {
    uint16_t kind, name, has_obj;
    uint32_t obj;
    uint8_t rgb[3];
    uint16_t x, y, w, h;
} DkPanel;

typedef struct DkTemplate {
    uint16_t kind, name;
    int npanels;                 /* base: DkPanel list; derived kinds 1/3: panel names */
    DkPanel *panels;
    uint16_t *panel_names;
    uint8_t rgb[3];
    uint16_t raw_rect[4];        /* as stored: w, h, x, y */
    size_t header_len;           /* base templates: raw header bytes */
} DkTemplate;

typedef struct DkStyle {
    uint16_t name, ref, value;
    uint8_t colours[4][3];
    char face[48];
} DkStyle;

typedef struct DkIndexEntry {
    uint16_t text;               /* string ref */
    uint16_t page;               /* object index */
    int npopups;
    const uint8_t *popups;       /* npopups big-endian u32 object indices (into file data) */
} DkIndexEntry;

typedef struct Dkmm {
    const uint8_t *data;         /* not owned */
    size_t size;
    char header[6][32];
    int nstrings;
    uint32_t *str_off;           /* string i (1-based) at data + str_off[i-1], length str_len[i-1] */
    uint16_t *str_len;
    uint16_t screen_w, screen_h;
    int nbases, ntemplates, nstyles;
    DkTemplate *bases, *templates;
    DkStyle *styles;
    int nindex;
    DkIndexEntry *index;
    int nobjects;
    uint32_t objtab_off, obj_base;
    uint32_t *obj_off, *obj_end;
} Dkmm;

int dkmm_open(Dkmm *dk, const uint8_t *data, size_t size, char *err, size_t errlen);
void dkmm_close(Dkmm *dk);

/* String ref (1-based) -> pointer to CP1250 bytes and length; NULL for 0 or out of range. */
const char *dkmm_string(const Dkmm *dk, uint16_t ref, size_t *len);

/* Parses object record i. Returns 0 on success; dkmm_object_free() releases it. */
int dkmm_object(const Dkmm *dk, int i, DkObject *out, char *err, size_t errlen);
void dkmm_object_free(DkObject *o);

#endif
