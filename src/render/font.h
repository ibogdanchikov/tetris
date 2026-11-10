#pragma once

#include <stdint.h>

/*
 * Tiny built-in 5x7 bitmap font. Pure data and arithmetic: no OpenGL, no SDL, so
 * the layout library and the tests can use it on their own.
 *
 * Each glyph is seven rows of five pixels. Rows are stored top row first and
 * bit 4 (0x10) is the leftmost pixel of a row. Only space, A-Z, 0-9 and
 * ":.-!?" have glyphs. Lowercase letters use the uppercase glyph; any other
 * character has no glyph and is drawn as an empty cell that still advances the
 * cursor, so an unsupported character leaves a gap rather than a box.
 */

#define FONT_GLYPH_W 5
#define FONT_GLYPH_H 7

/* One pixel column between two glyphs. */
#define FONT_GLYPH_SPACING 1

/* Glyph rows for c, or NULL when the font has no glyph for it. */
const uint8_t *font_glyph(char c);

/*
 * Width of text in pixels at the given glyph pixel size: n glyphs take
 * n * FONT_GLYPH_W * pixel plus one pixel column between each pair. Returns 0
 * for an empty or NULL string and when pixel is less than 1.
 */
int font_text_width(const char *text, int pixel);
