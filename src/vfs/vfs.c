/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "vfs/vfs.h"
#include "vfs/vfs_internal.h"

#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void vfs_normalise(const char *in, char *out, size_t outlen)
{
    size_t o = 0;
    while (*in == '/' || *in == '\\')
        in++;
    for (; *in && o + 1 < outlen; in++) {
        char c = *in;
        if (c == ';')
            break;                    /* ISO 9660 version suffix */
        if (c == '\\')
            c = '/';
        out[o++] = (char)toupper((unsigned char)c);
    }
    out[o] = '\0';
}

int vfs_add_entry(struct Vfs *vfs, const char *name, const char *host_path, uint32_t extent, uint32_t size)
{
    if (vfs->count == vfs->cap) {
        size_t cap = vfs->cap ? vfs->cap * 2 : 1024;
        VfsEntry *e = realloc(vfs->entries, cap * sizeof *e);
        if (!e)
            return -1;
        vfs->entries = e;
        vfs->cap = cap;
    }
    char norm[512];
    vfs_normalise(name, norm, sizeof norm);
    VfsEntry *e = &vfs->entries[vfs->count];
    e->name = SDL_strdup(norm);
    e->host_path = host_path ? SDL_strdup(host_path) : NULL;
    e->extent = extent;
    e->size = size;
    if (!e->name || (host_path && !e->host_path))
        return -1;
    vfs->count++;
    return 0;
}

static int cmp_entry(const void *a, const void *b)
{
    return strcmp(((const VfsEntry *)a)->name, ((const VfsEntry *)b)->name);
}

/* ---------------------------------------------------------------- directory source */

typedef struct DirWalk {
    struct Vfs *vfs;
    const char *root;
    size_t root_len;
    int failed;
} DirWalk;

static SDL_EnumerationResult SDLCALL dir_cb(void *userdata, const char *dirname, const char *fname)
{
    DirWalk *w = userdata;
    char full[1024];
    SDL_snprintf(full, sizeof full, "%s%s", dirname, fname);
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(full, &info))
        return SDL_ENUM_CONTINUE;
    if (info.type == SDL_PATHTYPE_DIRECTORY) {
        if (!SDL_EnumerateDirectory(full, dir_cb, w))
            w->failed = 1;
    } else if (info.type == SDL_PATHTYPE_FILE && fname[0] != '.') {
        const char *rel = full + w->root_len;
        if (vfs_add_entry(w->vfs, rel, full, 0, (uint32_t)info.size) != 0)
            w->failed = 1;
    }
    return w->failed ? SDL_ENUM_FAILURE : SDL_ENUM_CONTINUE;
}

static int scan_directory(struct Vfs *vfs, const char *path, char *err, size_t errlen)
{
    char root[1024];
    size_t n = SDL_strlcpy(root, path, sizeof root);
    if (n && root[n - 1] != '/' && root[n - 1] != '\\')
        SDL_strlcat(root, "/", sizeof root);
    DirWalk w = {vfs, root, SDL_strlen(root), 0};
    if (!SDL_EnumerateDirectory(root, dir_cb, &w) || w.failed) {
        SDL_snprintf(err, errlen, "cannot read directory %s: %s", root, SDL_GetError());
        return -1;
    }
    return 0;
}

/* ---------------------------------------------------------------- public API */

Vfs *vfs_open(const char *path, char *err, size_t errlen)
{
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info)) {
        SDL_snprintf(err, errlen, "%s: not found", path);
        return NULL;
    }
    struct Vfs *vfs = SDL_calloc(1, sizeof *vfs);
    if (!vfs) {
        SDL_snprintf(err, errlen, "out of memory");
        return NULL;
    }
    int rc;
    if (info.type == SDL_PATHTYPE_DIRECTORY) {
        rc = scan_directory(vfs, path, err, errlen);
    } else {
        vfs->iso = SDL_IOFromFile(path, "rb");
        if (!vfs->iso) {
            SDL_snprintf(err, errlen, "%s: %s", path, SDL_GetError());
            rc = -1;
        } else {
            rc = iso9660_scan(vfs, err, errlen);
        }
    }
    if (rc != 0) {
        vfs_close(vfs);
        return NULL;
    }
    qsort(vfs->entries, vfs->count, sizeof *vfs->entries, cmp_entry);
    return vfs;
}

void vfs_close(Vfs *vfs)
{
    if (!vfs)
        return;
    for (size_t i = 0; i < vfs->count; i++) {
        SDL_free(vfs->entries[i].name);
        SDL_free(vfs->entries[i].host_path);
    }
    free(vfs->entries);
    if (vfs->iso)
        SDL_CloseIO(vfs->iso);
    SDL_free(vfs);
}

static const VfsEntry *find(const Vfs *vfs, const char *path)
{
    char norm[512];
    vfs_normalise(path, norm, sizeof norm);
    size_t lo = 0, hi = vfs->count;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        int c = strcmp(vfs->entries[mid].name, norm);
        if (c == 0)
            return &vfs->entries[mid];
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return NULL;
}

int vfs_exists(Vfs *vfs, const char *path) { return find(vfs, path) != NULL; }

uint8_t *vfs_read(Vfs *vfs, const char *path, size_t *size)
{
    const VfsEntry *e = find(vfs, path);
    if (!e)
        return NULL;
    uint8_t *buf = malloc((size_t)e->size + 1);
    if (!buf)
        return NULL;
    size_t got = 0;
    if (e->host_path) {
        SDL_IOStream *io = SDL_IOFromFile(e->host_path, "rb");
        if (io) {
            got = SDL_ReadIO(io, buf, e->size);
            SDL_CloseIO(io);
        }
    } else if (SDL_SeekIO(vfs->iso, (Sint64)e->extent * 2048, SDL_IO_SEEK_SET) >= 0) {
        got = SDL_ReadIO(vfs->iso, buf, e->size);
    }
    if (got != e->size) {
        free(buf);
        return NULL;
    }
    buf[e->size] = 0;
    if (size)
        *size = e->size;
    return buf;
}

size_t vfs_count(const Vfs *vfs) { return vfs->count; }
const char *vfs_name(const Vfs *vfs, size_t index) { return index < vfs->count ? vfs->entries[index].name : NULL; }
