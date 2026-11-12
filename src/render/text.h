#pragma once

#include "render/renderer.h"

/*
 * Draws strings. With the renderer's font atlas (renderer_has_font) every
 * glyph is one quad of the stroke font, render/stroke_font.h: neon tubes that
 * stay smooth at any size, glow included. Without it the built-in 5x7 pixel
 * font is drawn as rectangles, one per horizontal run of lit pixels.
 *
 * Both fonts share the pixel font's metrics, so a layout does not depend on
 * which one draws: a glyph is FONT_GLYPH_W x FONT_GLYPH_H units, followed by
 * FONT_GLYPH_SPACING units of space.
 *
 * x and y are the bottom-left corner of the text box: the first glyph starts at
 * x and the bottom of the last row is y (the renderer is y-up).
 *
 * pixel is the size of one unit in framebuffer pixels. The stroke font takes
 * any size, fractions included; the pixel font rounds it down to a whole pixel
 * (at least 1). A character the font has no glyph for is skipped but still
 * advances the cursor.
 *
 * glow is a neon glow drawn behind the letters with additive blending, in its
 * own color; its alpha is the strength, and 0 draws no glow. The letters
 * themselves stay crisp. Leaves the blend mode at alpha.
 */
void text_draw(Renderer *renderer, float x, float y, float pixel, Color color, Color glow,
               const char *text);

/* Draws text centered both horizontally and vertically inside a box. */
void text_draw_centered(Renderer *renderer, float x, float y, float w, float h, float pixel,
                        Color color, Color glow, const char *text);
