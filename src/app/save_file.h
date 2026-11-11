#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "app/save.h"

/*
 * Where the save file lives and how it is read and written. This is the SDL
 * half of saving; the format itself is in src/app/save.h.
 *
 * The file is SAVE_FILE_NAME inside SDL_GetPrefPath("", SAVE_FILE_APP), for
 * example ~/.local/share/tetris/save.txt on Linux.
 */

#define SAVE_FILE_APP "tetris"
#define SAVE_FILE_NAME "save.txt"

/*
 * Writes the full path of the save file into buffer. SDL_GetPrefPath creates
 * the directory when it is missing. Returns false after logging when there is
 * no preference directory; the game then runs without saving.
 */
bool save_file_path(char *buffer, size_t size);

/*
 * Loads the save file into out. A missing file is normal on the first run and
 * gives the defaults quietly; an unreadable or corrupt one logs a warning and
 * also gives the defaults. out is always valid afterwards.
 */
void save_file_load(const char *path, SaveData *out);

/*
 * Writes the save file: the text goes to "<path>.tmp" first, which is then
 * renamed over the old file, so a crash in the middle of a save leaves the old
 * file as it was. Returns false after logging on any failure.
 */
bool save_file_write(const char *path, const SaveData *save);
