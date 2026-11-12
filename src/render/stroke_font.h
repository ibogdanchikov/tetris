#pragma once

#include <stdbool.h>

/*
 * The vector font: every glyph is a few strokes (polylines) on the same 5x7
 * grid as the pixel font in render/font.h, drawn as round-capped tubes like
 * neon signs. Pure data and arithmetic, no GL and no SDL, so the tests can
 * check it.
 *
 * stroke_font_build_atlas turns the strokes into a signed-distance atlas: one
 * cell per glyph, each texel holding how far it is from the nearest stroke.
 * The renderer uploads it as a texture and the quad shader draws glyphs from
 * it at any size, with a crisp edge at the tube and a glow that falls off
 * outside it. The glyph box, the advance and the spacing match the pixel font
 * exactly, so every layout works the same with either font.
 *
 * Units: one unit is one pixel of the 5x7 pixel font. A glyph box is
 * FONT_GLYPH_W x FONT_GLYPH_H units with its origin at the bottom left (y up),
 * and every atlas cell adds STROKE_FONT_PAD units of room on each side for the
 * glow.
 */

/* Half the tube width, in units. */
#define STROKE_FONT_RADIUS 0.45f

/* Room around the glyph box in each atlas cell, in units. */
#define STROKE_FONT_PAD 1.5f

/* Distance past the tube edge at which the stored value reaches 0, in units. */
#define STROKE_FONT_SPREAD 1.4f

/* Atlas resolution and layout. */
#define STROKE_FONT_TEXELS_PER_UNIT 8
#define STROKE_FONT_COLUMNS 8

/*
 * Index of the glyph for c in the atlas, or -1 when the font has nothing to
 * draw for it (a space, or a character outside the set). Lowercase letters use
 * the uppercase glyph. The set is the pixel font's: A-Z, 0-9 and ":.-!?<>".
 */
int stroke_font_glyph_index(char c);

/* Number of glyphs in the atlas. */
int stroke_font_glyph_count(void);

/* Atlas cell and atlas size in texels. Rows = glyph count / columns, rounded up. */
void stroke_font_cell_size(int *out_width, int *out_height);
void stroke_font_atlas_size(int *out_width, int *out_height);

/*
 * Fills an 8-bit atlas of exactly stroke_font_atlas_size texels, row 0 at the
 * bottom (the way OpenGL reads it). Each texel is
 *
 *     0.5 + (STROKE_FONT_RADIUS - distance) / (2 * STROKE_FONT_SPREAD)
 *
 * clamped to 0..1 and scaled to 0..255: 128 is the edge of the tube, above it
 * is inside, and it reaches 0 STROKE_FONT_SPREAD units outside. Glyph i sits
 * in column i % STROKE_FONT_COLUMNS and row i / STROKE_FONT_COLUMNS. Returns
 * false, writing nothing, when the size does not match.
 */
bool stroke_font_build_atlas(unsigned char *pixels, int width, int height);
