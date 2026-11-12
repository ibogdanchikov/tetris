#include "render/text.h"

#include <stddef.h>
#include <string.h>

#include "render/font.h"
#include "render/stroke_font.h"

/* How far a letter's glow reaches, in font pixels. */
#define TEXT_GLOW_REACH_PIXELS 1.5f

/*
 * Draws one glyph. Each row is scanned left to right and every run of lit pixels
 * becomes a single rectangle, so ".###." is one instance instead of three. With
 * glow set, each run becomes a filled neon shape grown by the glow reach
 * instead, and the overlapping glows of the runs add up to the letter's shape.
 */
static void text_draw_glyph(Renderer *renderer, float x, float y, int pixel, Color color,
                            bool glow, const uint8_t *rows)
{
    const float reach = glow ? TEXT_GLOW_REACH_PIXELS * (float)pixel : 0.0f;

    for (int row = 0; row < FONT_GLYPH_H; ++row) {
        const uint8_t bits = rows[row];
        int column = 0;
        while (column < FONT_GLYPH_W) {
            const uint8_t mask = (uint8_t)(1u << (FONT_GLYPH_W - 1 - column));
            if ((bits & mask) == 0) {
                ++column;
                continue;
            }

            int run = 0;
            while (column + run < FONT_GLYPH_W &&
                   (bits & (uint8_t)(1u << (FONT_GLYPH_W - 1 - (column + run)))) != 0) {
                ++run;
            }

            const float run_x = x + (float)(column * pixel);
            const float run_y = y + (float)((FONT_GLYPH_H - 1 - row) * pixel);
            if (glow) {
                renderer_push_styled(renderer, run_x - reach, run_y - reach,
                                     (float)(run * pixel) + 2.0f * reach,
                                     (float)pixel + 2.0f * reach, color, RECT_STYLE_NEON, 0.0f,
                                     reach, 0.0f);
            } else {
                renderer_push_rect(renderer, run_x, run_y, (float)(run * pixel), (float)pixel,
                                   color);
            }
            column += run;
        }
    }
}

/* One pass over the string, either the glow or the crisp letters. */
static void text_draw_pass(Renderer *renderer, float x, float y, int pixel, Color color,
                           bool glow, const char *text)
{
    const float advance = (float)((FONT_GLYPH_W + FONT_GLYPH_SPACING) * pixel);
    float cursor = x;

    for (const char *c = text; *c != '\0'; ++c) {
        const uint8_t *rows = font_glyph(*c);
        if (rows != NULL) {
            text_draw_glyph(renderer, cursor, y, pixel, color, glow, rows);
        }
        cursor += advance;
    }
}

/*
 * One pass of the stroke font: a quad per glyph covering its whole atlas cell,
 * so the cell's padding lands STROKE_FONT_PAD units around the glyph box.
 */
static void text_draw_stroke_pass(Renderer *renderer, float x, float y, float pixel,
                                  Color color, bool glow, const char *text)
{
    const float advance = (float)(FONT_GLYPH_W + FONT_GLYPH_SPACING) * pixel;
    const float pad = STROKE_FONT_PAD * pixel;
    const float w = (float)FONT_GLYPH_W * pixel + 2.0f * pad;
    const float h = (float)FONT_GLYPH_H * pixel + 2.0f * pad;
    float cursor = x;

    for (const char *c = text; *c != '\0'; ++c) {
        const int glyph = stroke_font_glyph_index(*c);
        if (glyph >= 0) {
            renderer_push_styled(renderer, cursor - pad, y - pad, w, h, color, RECT_STYLE_GLYPH,
                                 (float)glyph, glow ? 1.0f : 0.0f, 0.0f);
        }
        cursor += advance;
    }
}

void text_draw(Renderer *renderer, float x, float y, float pixel, Color color, Color glow,
               const char *text)
{
    if (text == NULL || !(pixel > 0.0f)) {
        return;
    }

    if (renderer_has_font(renderer)) {
        if (glow.a > 0.0f) {
            renderer_set_blend(renderer, RENDER_BLEND_ADDITIVE);
            text_draw_stroke_pass(renderer, x, y, pixel, glow, true, text);
            renderer_set_blend(renderer, RENDER_BLEND_ALPHA);
        }
        text_draw_stroke_pass(renderer, x, y, pixel, color, false, text);
        return;
    }

    /* The pixel font only has whole pixels. */
    const int whole = pixel < 1.0f ? 1 : (int)pixel;
    if (glow.a > 0.0f) {
        renderer_set_blend(renderer, RENDER_BLEND_ADDITIVE);
        text_draw_pass(renderer, x, y, whole, glow, true, text);
        renderer_set_blend(renderer, RENDER_BLEND_ALPHA);
    }
    text_draw_pass(renderer, x, y, whole, color, false, text);
}

/* Rounds to the nearest pixel so centered text keeps crisp edges. */
static float text_round(float value)
{
    return (float)(int)(value + (value >= 0.0f ? 0.5f : -0.5f));
}

void text_draw_centered(Renderer *renderer, float x, float y, float w, float h, float pixel,
                        Color color, Color glow, const char *text)
{
    if (text == NULL || !(pixel > 0.0f)) {
        return;
    }

    /* The same width font_text_width gives for whole pixels, at any size. */
    const size_t glyphs = strlen(text);
    const float text_w =
        glyphs == 0 ? 0.0f
                    : ((float)glyphs * (float)(FONT_GLYPH_W + FONT_GLYPH_SPACING) -
                       (float)FONT_GLYPH_SPACING) * pixel;
    const float text_h = (float)FONT_GLYPH_H * pixel;
    text_draw(renderer, text_round(x + (w - text_w) * 0.5f),
              text_round(y + (h - text_h) * 0.5f), pixel, color, glow, text);
}
