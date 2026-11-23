/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Persistent user settings: a small "key=value" text file in the SDL preferences folder
 * (e.g. ~/.local/share/twtw/Jak to dziala/settings.cfg on Linux), rewritten on every save.
 * Nothing here affects the game's behaviour, only where its data is and how it is shown. */
#ifndef TWTW_SETTINGS_H
#define TWTW_SETTINGS_H

typedef struct Settings {
    char data[1024];        /* the CD image or extracted CD folder last used */
    int fullscreen;         /* 1 = start fullscreen */
    int integer_scale;      /* 1 = scale by whole multiples only (sharp pixels, black borders) */
} Settings;

/* Loads the settings (defaults when the file doesn't exist). */
void settings_load(Settings *s);
/* Saves them; returns 0 on success. */
int settings_save(const Settings *s);
/* The settings file's path (for messages), or "" if SDL can't provide a preferences folder. */
const char *settings_path(void);

#endif
