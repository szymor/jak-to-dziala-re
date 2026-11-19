/* twtw-tool: headless checks and dumps for the port's loaders.
 *
 *   twtw-tool ls DATA                  list files (DATA = .iso or extracted directory)
 *   twtw-tool check-vfs ISO DIR        compare the ISO reader against an extracted copy
 *   twtw-tool dump-dibs DATA OUTDIR    decode every .DIB to OUTDIR/<path>.idx
 *                                      (format: "W H\n", W*H indices top-down, 768 bytes RGB palette)
 */
#include "formats/dib.h"
#include "vfs/vfs.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Vfs *open_or_die(const char *path)
{
    char err[256];
    Vfs *v = vfs_open(path, err, sizeof err);
    if (!v) {
        fprintf(stderr, "error: %s\n", err);
        exit(1);
    }
    return v;
}

static int ends_with(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && strcmp(s + a - b, suffix) == 0;
}

static int cmd_ls(const char *data)
{
    Vfs *v = open_or_die(data);
    for (size_t i = 0; i < vfs_count(v); i++)
        puts(vfs_name(v, i));
    vfs_close(v);
    return 0;
}

static int cmd_check_vfs(const char *iso, const char *dir)
{
    Vfs *a = open_or_die(iso), *b = open_or_die(dir);
    int bad = 0;
    size_t n = vfs_count(a);
    for (size_t i = 0; i < n; i++) {
        const char *name = vfs_name(a, i);
        size_t sa = 0, sb = 0;
        uint8_t *da = vfs_read(a, name, &sa), *db = vfs_read(b, name, &sb);
        if (!da || !db || sa != sb || memcmp(da, db, sa) != 0) {
            printf("MISMATCH %s (iso %s %zu, dir %s %zu)\n", name, da ? "ok" : "missing", sa,
                   db ? "ok" : "missing", sb);
            bad++;
        }
        free(da);
        free(db);
    }
    printf("%zu files in ISO, %zu in directory, %d mismatches\n", n, vfs_count(b), bad);
    vfs_close(a);
    vfs_close(b);
    return bad ? 1 : 0;
}

static int cmd_dump_dibs(const char *data, const char *outdir)
{
    Vfs *v = open_or_die(data);
    int ok = 0, bad = 0;
    for (size_t i = 0; i < vfs_count(v); i++) {
        const char *name = vfs_name(v, i);
        if (!ends_with(name, ".DIB"))
            continue;
        size_t size;
        uint8_t *buf = vfs_read(v, name, &size);
        Dib dib;
        char err[128];
        if (!buf || dib_decode(buf, size, &dib, err, sizeof err) != 0) {
            printf("FAIL %s: %s\n", name, buf ? err : "read error");
            bad++;
            free(buf);
            continue;
        }
        char path[1024], flat[512];
        SDL_strlcpy(flat, name, sizeof flat);
        for (char *p = flat; *p; p++)
            if (*p == '/')
                *p = '_';
        snprintf(path, sizeof path, "%s/%s.idx", outdir, flat);
        FILE *f = fopen(path, "wb");
        if (f) {
            fprintf(f, "%d %d\n", dib.width, dib.height);
            fwrite(dib.pixels, 1, (size_t)dib.width * dib.height, f);
            fwrite(dib.palette, 1, sizeof dib.palette, f);
            fclose(f);
            ok++;
        } else {
            bad++;
        }
        dib_free(&dib);
        free(buf);
    }
    printf("%d bitmaps decoded, %d failed\n", ok, bad);
    vfs_close(v);
    return bad ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "ls"))
        return cmd_ls(argv[2]);
    if (argc >= 4 && !strcmp(argv[1], "check-vfs"))
        return cmd_check_vfs(argv[2], argv[3]);
    if (argc >= 4 && !strcmp(argv[1], "dump-dibs"))
        return cmd_dump_dibs(argv[2], argv[3]);
    fprintf(stderr, "usage: twtw-tool ls DATA | check-vfs ISO DIR | dump-dibs DATA OUTDIR\n");
    return 2;
}
