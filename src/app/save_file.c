#include "app/save_file.h"

#include <stdio.h>

#include <SDL3/SDL.h>

bool save_file_path(char *buffer, size_t size)
{
    /* An empty organization puts the directory straight under the user's data
       directory, named after the game. */
    char *dir = SDL_GetPrefPath("", SAVE_FILE_APP);
    if (dir == NULL) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "saving disabled: SDL_GetPrefPath: %s",
                    SDL_GetError());
        return false;
    }

    /* SDL_GetPrefPath ends in a separator. */
    const int written = snprintf(buffer, size, "%s%s", dir, SAVE_FILE_NAME);
    SDL_free(dir);
    if (written < 0 || (size_t)written >= size) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "saving disabled: save path is too long");
        return false;
    }
    return true;
}

void save_file_load(const char *path, SaveData *out)
{
    save_defaults(out);

    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info)) {
        SDL_Log("No save file at %s yet, using the defaults", path);
        return;
    }

    size_t length = 0;
    char *text = SDL_LoadFile(path, &length);
    if (text == NULL) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "could not read %s: %s; using the defaults",
                    path, SDL_GetError());
        return;
    }

    if (!save_parse(text, length, out)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "%s is not a save file this version can read; using the defaults", path);
    }
    SDL_free(text);
}

bool save_file_write(const char *path, const SaveData *save)
{
    char text[SAVE_TEXT_CAPACITY];
    const size_t length = save_serialize(save, text, sizeof text);
    if (length == 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "save data does not fit %d bytes",
                    SAVE_TEXT_CAPACITY);
        return false;
    }

    char temp_path[1100];
    const int written = snprintf(temp_path, sizeof temp_path, "%s.tmp", path);
    if (written < 0 || (size_t)written >= sizeof temp_path) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "save path is too long: %s", path);
        return false;
    }

    if (!SDL_SaveFile(temp_path, text, length)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "could not write %s: %s", temp_path,
                    SDL_GetError());
        (void)SDL_RemovePath(temp_path);
        return false;
    }
    if (!SDL_RenamePath(temp_path, path)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "could not replace %s: %s", path,
                    SDL_GetError());
        (void)SDL_RemovePath(temp_path);
        return false;
    }
    return true;
}
