#pragma once

#include "render/renderer.h"

/*
 * Draws strings with the built-in 5x7 font as rectangles, one per horizontal run
 * of lit pixels, so a glyph costs at most seven instances rather than up to 35.
 *
 * x and y are the bottom-left corner of the text box: the first glyph starts at
 * x and the bottom of the last row is y (the renderer is y-up).
 *
 * pixel is the edge length of one font pixel in framebuffer pixels. A character
 * the font has no glyph for is skipped but still advances the cursor.
 */
void text_draw(Renderer *renderer, float x, float y, int pixel, Color color, const char *text);

/* Draws text centred both horizontally and vertically inside a box. */
void text_draw_centered(Renderer *renderer, float x, float y, float w, float h, int pixel,
                        Color color, const char *text);
