#include "app/save_file.h"

#include <stdlib.h>

#include <SDL3/SDL.h>
#include <emscripten.h>

/*
 * The browser build of save_file.h. Files a page writes live only in memory
 * and are gone on the next visit, so the save text goes to the page's
 * localStorage instead, under the key that save_file_path returns. One
 * setItem replaces the whole text at once, so there is no temporary copy.
 *
 * Every call is wrapped in try: a browser can refuse localStorage, for example
 * when the player blocks site data, and the game then runs without saving.
 */

EM_JS_DEPS(save_file_web, "$stringToNewUTF8,$UTF8ToString")

EM_JS(int, save_file_web_available, (void), {
    try {
        return typeof localStorage === 'object' && localStorage !== null ? 1 : 0;
    } catch (e) {
        return 0;
    }
})

/* The text stored under key, in memory from malloc, or NULL when there is none. */
EM_JS(char *, save_file_web_get, (const char *key), {
    try {
        const text = localStorage.getItem(UTF8ToString(key));
        return text === null ? 0 : stringToNewUTF8(text);
    } catch (e) {
        return 0;
    }
})

EM_JS(int, save_file_web_set, (const char *key, const char *text), {
    try {
        localStorage.setItem(UTF8ToString(key), UTF8ToString(text));
        return 1;
    } catch (e) {
        return 0;
    }
})

bool save_file_path(char *buffer, size_t size)
{
    if (!save_file_web_available()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "saving disabled: no localStorage");
        return false;
    }
    SDL_strlcpy(buffer, SAVE_FILE_APP "/" SAVE_FILE_NAME, size);
    return true;
}

void save_file_load(const char *path, SaveData *out)
{
    save_defaults(out);

    char *text = save_file_web_get(path);
    if (text == NULL) {
        SDL_Log("No saved data under %s yet, using the defaults", path);
        return;
    }
    if (!save_parse(text, SDL_strlen(text), out)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "%s is not a save this version can read; using the defaults", path);
    }
    free(text);
}

bool save_file_write(const char *path, const SaveData *save)
{
    char text[SAVE_TEXT_CAPACITY];
    if (save_serialize(save, text, sizeof text) == 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "save data does not fit %d bytes",
                    SAVE_TEXT_CAPACITY);
        return false;
    }
    if (!save_file_web_set(path, text)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "could not store %s in localStorage", path);
        return false;
    }
    return true;
}
