/* Minimal ISO 9660 reader: primary volume descriptor and the directory tree.
 * Only what the game CD needs: 2048-byte logical blocks, single-extent files,
 * 8.3 names (the ";1" version suffix is stripped by vfs_normalise). */
#include "vfs/vfs_internal.h"
#include "util/bytes.h"

#include <stdlib.h>
#include <string.h>

#define BLOCK 2048

static int read_block(SDL_IOStream *io, uint32_t lba, uint32_t count, uint8_t *dst)
{
    if (SDL_SeekIO(io, (Sint64)lba * BLOCK, SDL_IO_SEEK_SET) < 0)
        return -1;
    size_t n = (size_t)count * BLOCK;
    return SDL_ReadIO(io, dst, n) == n ? 0 : -1;
}

static int walk(struct Vfs *vfs, uint32_t extent, uint32_t size, const char *prefix, int depth)
{
    if (depth > 16 || size > 16u * 1024 * 1024)
        return -1;
    uint32_t blocks = (size + BLOCK - 1) / BLOCK;
    uint8_t *dir = malloc((size_t)blocks * BLOCK);
    if (!dir || read_block(vfs->iso, extent, blocks, dir) != 0) {
        free(dir);
        return -1;
    }
    int rc = 0;
    for (uint32_t off = 0; off < size && rc == 0;) {
        uint8_t len = dir[off];
        if (len == 0) {                       /* records don't cross block boundaries */
            off = (off / BLOCK + 1) * BLOCK;
            continue;
        }
        if (off + len > blocks * BLOCK || len < 34)
            break;
        const uint8_t *r = dir + off;
        uint32_t ext = rd_le32(r + 2), sz = rd_le32(r + 10);
        uint8_t flags = r[25], nlen = r[32];
        const char *name = (const char *)r + 33;
        off += len;
        if (nlen == 1 && (name[0] == 0 || name[0] == 1))
            continue;                         /* "." and ".." */
        char path[512];
        SDL_snprintf(path, sizeof path, "%s%.*s", prefix, (int)nlen, name);
        if (flags & 2) {
            SDL_strlcat(path, "/", sizeof path);
            rc = walk(vfs, ext, sz, path, depth + 1);
        } else {
            size_t pl = SDL_strlen(path);     /* "NAME." with no extension -> "NAME" */
            char *semi = SDL_strchr(path, ';');
            if (semi && semi > path && semi[-1] == '.')
                memmove(semi - 1, semi, pl - (size_t)(semi - path) + 1);
            rc = vfs_add_entry(vfs, path, NULL, ext, sz);
        }
    }
    free(dir);
    return rc;
}

int iso9660_scan(struct Vfs *vfs, char *err, size_t errlen)
{
    uint8_t pvd[BLOCK];
    for (uint32_t lba = 16; lba < 32; lba++) {
        if (read_block(vfs->iso, lba, 1, pvd) != 0)
            break;
        if (memcmp(pvd + 1, "CD001", 5) != 0)
            break;
        if (pvd[0] == 1) {                    /* primary volume descriptor */
            if (rd_le16(pvd + 128) != BLOCK) {
                SDL_snprintf(err, errlen, "unsupported ISO block size %u", rd_le16(pvd + 128));
                return -1;
            }
            const uint8_t *root = pvd + 156;
            if (walk(vfs, rd_le32(root + 2), rd_le32(root + 10), "", 0) != 0) {
                SDL_snprintf(err, errlen, "corrupt ISO 9660 directory tree");
                return -1;
            }
            return 0;
        }
        if (pvd[0] == 255)
            break;
    }
    SDL_snprintf(err, errlen, "not an ISO 9660 image (no primary volume descriptor)");
    return -1;
}
