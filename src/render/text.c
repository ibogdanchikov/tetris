#include "render/text.h"

#include <stddef.h>

#include "render/font.h"

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

void text_draw(Renderer *renderer, float x, float y, int pixel, Color color, Color glow,
               const char *text)
{
    if (text == NULL || pixel < 1) {
        return;
    }

    if (glow.a > 0.0f) {
        renderer_set_blend(renderer, RENDER_BLEND_ADDITIVE);
        text_draw_pass(renderer, x, y, pixel, glow, true, text);
        renderer_set_blend(renderer, RENDER_BLEND_ALPHA);
    }
    text_draw_pass(renderer, x, y, pixel, color, false, text);
}

/* Rounds to the nearest pixel so centered text keeps crisp edges. */
static float text_round(float value)
{
    return (float)(int)(value + (value >= 0.0f ? 0.5f : -0.5f));
}

void text_draw_centered(Renderer *renderer, float x, float y, float w, float h, int pixel,
                        Color color, Color glow, const char *text)
{
    if (text == NULL || pixel < 1) {
        return;
    }

    const float text_w = (float)font_text_width(text, pixel);
    const float text_h = (float)(FONT_GLYPH_H * pixel);
    text_draw(renderer, text_round(x + (w - text_w) * 0.5f),
              text_round(y + (h - text_h) * 0.5f), pixel, color, glow, text);
}
