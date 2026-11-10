/*
 * Built-in font suite: glyph coverage, lowercase mapping, width arithmetic and
 * the documented behaviour for characters the font does not have. The font is
 * plain data, so nothing here touches SDL or OpenGL.
 */

#include "render/font.h"
#include "test_harness.h"

static const char FONT_SUPPORTED[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:.-!?";

static void test_font_every_supported_char_has_a_glyph(void)
{
    for (const char *c = FONT_SUPPORTED; *c != '\0'; ++c) {
        const uint8_t *rows = font_glyph(*c);
        CHECK(rows != NULL);
        if (rows == NULL) {
            continue;
        }

        int lit = 0;
        for (int row = 0; row < FONT_GLYPH_H; ++row) {
            /* Rows are five pixels wide; the top three bits must stay clear. */
            CHECK((rows[row] & (uint8_t)~0x1Fu) == 0);
            for (int bit = 0; bit < FONT_GLYPH_W; ++bit) {
                if ((rows[row] & (uint8_t)(1u << bit)) != 0) {
                    ++lit;
                }
            }
        }

        if (*c == ' ') {
            CHECK_EQ_INT(lit, 0);
        } else {
            CHECK(lit > 0);
        }
    }
}

static void test_font_lowercase_maps_to_uppercase(void)
{
    for (char c = 'a'; c <= 'z'; ++c) {
        const char upper = (char)(c - 'a' + 'A');
        CHECK(font_glyph(c) == font_glyph(upper));
        CHECK(font_glyph(c) != NULL);
    }
}

static void test_font_unsupported_chars(void)
{
    /* No glyph: drawing skips the cell, but the width still counts it. */
    CHECK(font_glyph('@') == NULL);
    CHECK(font_glyph('_') == NULL);
    CHECK(font_glyph('#') == NULL);
    CHECK(font_glyph('\n') == NULL);
    CHECK(font_glyph('\0') == NULL);

    CHECK_EQ_INT(font_text_width("@", 2), FONT_GLYPH_W * 2);
}

static void test_font_width(void)
{
    CHECK_EQ_INT(font_text_width("", 3), 0);
    CHECK_EQ_INT(font_text_width(NULL, 3), 0);
    CHECK_EQ_INT(font_text_width("A", 0), 0);
    CHECK_EQ_INT(font_text_width("A", -2), 0);

    CHECK_EQ_INT(font_text_width("A", 1), FONT_GLYPH_W);
    CHECK_EQ_INT(font_text_width("A", 3), FONT_GLYPH_W * 3);
    CHECK_EQ_INT(font_text_width("AB", 3), 2 * FONT_GLYPH_W * 3 + 3);

    /* n glyphs: n * width plus one column between each pair. */
    const char *text = "ABCDEFGHIJ";
    for (int n = 1; n <= 10; ++n) {
        const int pixel = 2;
        const int expected = n * FONT_GLYPH_W * pixel + (n - 1) * pixel;
        char buffer[11];
        for (int i = 0; i < n; ++i) {
            buffer[i] = text[i];
        }
        buffer[n] = '\0';
        CHECK_EQ_INT(font_text_width(buffer, pixel), expected);
    }
}

void test_font_suite(void)
{
    test_font_every_supported_char_has_a_glyph();
    test_font_lowercase_maps_to_uppercase();
    test_font_unsupported_chars();
    test_font_width();
}
