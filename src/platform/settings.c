/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "platform/settings.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char path_buf[1200];

const char *settings_path(void)
{
    if (!path_buf[0]) {
        char *dir = SDL_GetPrefPath("twtw", "Jak to dziala");
        if (dir) {
            SDL_snprintf(path_buf, sizeof path_buf, "%ssettings.cfg", dir);
            SDL_free(dir);
        }
    }
    return path_buf;
}

void settings_load(Settings *s)
{
    memset(s, 0, sizeof *s);
    const char *path = settings_path();
    if (!path[0])
        return;
    size_t len;
    char *text = SDL_LoadFile(path, &len);
    if (!text)
        return;
    for (char *line = text, *next; line && *line; line = next) {
        next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        size_t n = strlen(line);
        if (n && line[n - 1] == '\r')
            line[n - 1] = 0;
        char *eq = strchr(line, '=');
        if (!eq || line[0] == '#')
            continue;
        *eq = 0;
        const char *key = line, *val = eq + 1;
        if (!strcmp(key, "data"))
            SDL_strlcpy(s->data, val, sizeof s->data);
        else if (!strcmp(key, "fullscreen"))
            s->fullscreen = atoi(val) != 0;
        else if (!strcmp(key, "integer_scale"))
            s->integer_scale = atoi(val) != 0;
    }
    SDL_free(text);
}

int settings_save(const Settings *s)
{
    const char *path = settings_path();
    if (!path[0])
        return -1;
    char text[1400];
    int n = SDL_snprintf(text, sizeof text,
                         "# Jak to dziala? port settings\n"
                         "data=%s\n"
                         "fullscreen=%d\n"
                         "integer_scale=%d\n",
                         s->data, s->fullscreen, s->integer_scale);
    if (n < 0 || (size_t)n >= sizeof text)
        return -1;
    return SDL_SaveFile(path, text, (size_t)n) ? 0 : -1;
}
