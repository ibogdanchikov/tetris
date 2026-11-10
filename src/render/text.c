#include "render/text.h"

#include <stddef.h>

#include "render/font.h"

/*
 * Draws one glyph. Each row is scanned left to right and every run of lit pixels
 * becomes a single rectangle, so ".###." is one instance instead of three.
 */
static void text_draw_glyph(Renderer *renderer, float x, float y, int pixel, Color color,
                            const uint8_t *rows)
{
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

            renderer_push_rect(renderer, x + (float)(column * pixel),
                               y + (float)((FONT_GLYPH_H - 1 - row) * pixel),
                               (float)(run * pixel), (float)pixel, color);
            column += run;
        }
    }
}

void text_draw(Renderer *renderer, float x, float y, int pixel, Color color, const char *text)
{
    if (text == NULL || pixel < 1) {
        return;
    }

    const float advance = (float)((FONT_GLYPH_W + FONT_GLYPH_SPACING) * pixel);
    float cursor = x;

    for (const char *c = text; *c != '\0'; ++c) {
        const uint8_t *rows = font_glyph(*c);
        if (rows != NULL) {
            text_draw_glyph(renderer, cursor, y, pixel, color, rows);
        }
        cursor += advance;
    }
}

/* Rounds to the nearest pixel so centred text keeps crisp edges. */
static float text_round(float value)
{
    return (float)(int)(value + (value >= 0.0f ? 0.5f : -0.5f));
}

void text_draw_centered(Renderer *renderer, float x, float y, float w, float h, int pixel,
                        Color color, const char *text)
{
    if (text == NULL || pixel < 1) {
        return;
    }

    const float text_w = (float)font_text_width(text, pixel);
    const float text_h = (float)(FONT_GLYPH_H * pixel);
    text_draw(renderer, text_round(x + (w - text_w) * 0.5f),
              text_round(y + (h - text_h) * 0.5f), pixel, color, text);
}
