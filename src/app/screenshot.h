#pragma once

#include <stdbool.h>

/*
 * Reads the current back buffer (call it after rendering, before swapping) and
 * writes it as a 32-bit BMP. The one place where GL and SDL meet outside the
 * platform layer.
 */
bool screenshot_save_bmp(int fb_width, int fb_height, const char *path);
