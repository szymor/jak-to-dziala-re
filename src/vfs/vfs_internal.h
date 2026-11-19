#ifndef TWTW_VFS_INTERNAL_H
#define TWTW_VFS_INTERNAL_H

#include <SDL3/SDL.h>
#include <stdint.h>

typedef struct VfsEntry {
    char *name;        /* normalised path */
    char *host_path;   /* directory source: full host path; ISO source: NULL */
    uint32_t extent;   /* ISO source: first logical block */
    uint32_t size;
} VfsEntry;

struct Vfs {
    SDL_IOStream *iso; /* NULL for a directory source */
    VfsEntry *entries;
    size_t count, cap;
};

int vfs_add_entry(struct Vfs *vfs, const char *name, const char *host_path, uint32_t extent, uint32_t size);
int iso9660_scan(struct Vfs *vfs, char *err, size_t errlen);

#endif
