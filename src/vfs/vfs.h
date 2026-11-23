/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Read-only virtual file system over the original game data.
 *
 * The source is either the CD image (an ISO 9660 file) or a directory that
 * holds the extracted CD. Paths are matched case-insensitively and may use
 * '/' or '\\'; listed names are upper-case with '/' separators, e.g.
 * "ARTICLE1/AIRP/AIRP0M.DIB".
 */
#ifndef TWTW_VFS_H
#define TWTW_VFS_H

#include <stddef.h>
#include <stdint.h>

typedef struct Vfs Vfs;

/* Opens a directory or an .iso file. On failure returns NULL and writes a message to err. */
Vfs *vfs_open(const char *path, char *err, size_t errlen);
void vfs_close(Vfs *vfs);

/* Reads a whole file. Returns a malloc'd buffer (caller frees) or NULL if not found. */
uint8_t *vfs_read(Vfs *vfs, const char *path, size_t *size);
int vfs_exists(Vfs *vfs, const char *path);

/* Enumerates all files in a stable (sorted) order. */
size_t vfs_count(const Vfs *vfs);
const char *vfs_name(const Vfs *vfs, size_t index);

/* Normalises a path into the listing form (upper-case, '/', no leading '/', no ";1"). */
void vfs_normalise(const char *in, char *out, size_t outlen);

#endif
