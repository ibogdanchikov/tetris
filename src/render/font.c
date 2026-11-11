#include "render/font.h"

#include <string.h>

/*
 * One glyph row from five pixel flags, leftmost pixel first. Writing the table
 * this way keeps the shapes readable and the bit order impossible to get wrong:
 * FONT_ROW(0, 1, 1, 1, 0) is ".###.".
 */
#define FONT_ROW(a, b, c, d, e) \
    ((uint8_t)(((a) << 4) | ((b) << 3) | ((c) << 2) | ((d) << 1) | (e)))

typedef struct FontEntry {
    char c;
    uint8_t rows[FONT_GLYPH_H];
} FontEntry;

/* Top row first. Columns are bit 4 (left) to bit 0 (right). */
static const FontEntry FONT_ENTRIES[] = {
    { ' ', { 0, 0, 0, 0, 0, 0, 0 } },

    { 'A', { FONT_ROW(0, 1, 1, 1, 0), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1),
             FONT_ROW(1, 1, 1, 1, 1), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1),
             FONT_ROW(1, 0, 0, 0, 1) } },
    { 'B', { FONT_ROW(1, 1, 1, 1, 0), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1),
             FONT_ROW(1, 1, 1, 1, 0), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1),
             FONT_ROW(1, 1, 1, 1, 0) } },
    { 'C', { FONT_ROW(0, 1, 1, 1, 0), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 0),
             FONT_ROW(1, 0, 0, 0, 0), FONT_ROW(1, 0, 0, 0, 0), FONT_ROW(1, 0, 0, 0, 1),
             FONT_ROW(0, 1, 1, 1, 0) } },
    { 'D', { FONT_ROW(1, 1, 1, 1, 0), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1),
             FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1),
             FONT_ROW(1, 1, 1, 1, 0) } },
    { 'E', { FONT_ROW(1, 1, 1, 1, 1), FONT_ROW(1, 0, 0, 0, 0), FONT_ROW(1, 0, 0, 0, 0),
             FONT_ROW(1, 1, 1, 1, 0), FONT_ROW(1, 0, 0, 0, 0), FONT_ROW(1, 0, 0, 0, 0),
             FONT_ROW(1, 1, 1, 1, 1) } },
    { 'F', { FONT_ROW(1, 1, 1, 1, 1), FONT_ROW(1, 0, 0, 0, 0), FONT_ROW(1, 0, 0, 0, 0),
             FONT_ROW(1, 1, 1, 1, 0), FONT_ROW(1, 0, 0, 0, 0), FONT_ROW(1, 0, 0, 0, 0),
             FONT_ROW(1, 0, 0, 0, 0) } },
    { 'G', { FONT_ROW(0, 1, 1, 1, 0), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 0),
             FONT_ROW(1, 0, 1, 1, 1), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1),
             FONT_ROW(0, 1, 1, 1, 1) } },
    { 'H', { FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1),
             FONT_ROW(1, 1, 1, 1, 1), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1),
             FONT_ROW(1, 0, 0, 0, 1) } },
    { 'I', { FONT_ROW(0, 1, 1, 1, 0), FONT_ROW(0, 0, 1, 0, 0), FONT_ROW(0, 0, 1, 0, 0),
             FONT_ROW(0, 0, 1, 0, 0), FONT_ROW(0, 0, 1, 0, 0), FONT_ROW(0, 0, 1, 0, 0),
             FONT_ROW(0, 1, 1, 1, 0) } },
    { 'J', { FONT_ROW(0, 0, 1, 1, 1), FONT_ROW(0, 0, 0, 1, 0), FONT_ROW(0, 0, 0, 1, 0),
             FONT_ROW(0, 0, 0, 1, 0), FONT_ROW(0, 0, 0, 1, 0), FONT_ROW(1, 0, 0, 1, 0),
             FONT_ROW(0, 1, 1, 0, 0) } },
    { 'K', { FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 1, 0), FONT_ROW(1, 0, 1, 0, 0),
             FONT_ROW(1, 1, 0, 0, 0), FONT_ROW(1, 0, 1, 0, 0), FONT_ROW(1, 0, 0, 1, 0),
             FONT_ROW(1, 0, 0, 0, 1) } },
    { 'L', { FONT_ROW(1, 0, 0, 0, 0), FONT_ROW(1, 0, 0, 0, 0), FONT_ROW(1, 0, 0, 0, 0),
             FONT_ROW(1, 0, 0, 0, 0), FONT_ROW(1, 0, 0, 0, 0), FONT_ROW(1, 0, 0, 0, 0),
             FONT_ROW(1, 1, 1, 1, 1) } },
    { 'M', { FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 1, 0, 1, 1), FONT_ROW(1, 0, 1, 0, 1),
             FONT_ROW(1, 0, 1, 0, 1), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1),
             FONT_ROW(1, 0, 0, 0, 1) } },
    { 'N', { FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 1, 0, 0, 1), FONT_ROW(1, 0, 1, 0, 1),
             FONT_ROW(1, 0, 0, 1, 1), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1),
             FONT_ROW(1, 0, 0, 0, 1) } },
    { 'O', { FONT_ROW(0, 1, 1, 1, 0), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1),
             FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1),
             FONT_ROW(0, 1, 1, 1, 0) } },
    { 'P', { FONT_ROW(1, 1, 1, 1, 0), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1),
             FONT_ROW(1, 1, 1, 1, 0), FONT_ROW(1, 0, 0, 0, 0), FONT_ROW(1, 0, 0, 0, 0),
             FONT_ROW(1, 0, 0, 0, 0) } },
    { 'Q', { FONT_ROW(0, 1, 1, 1, 0), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1),
             FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 1, 0, 1), FONT_ROW(1, 0, 0, 1, 0),
             FONT_ROW(0, 1, 1, 0, 1) } },
    { 'R', { FONT_ROW(1, 1, 1, 1, 0), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1),
             FONT_ROW(1, 1, 1, 1, 0), FONT_ROW(1, 0, 1, 0, 0), FONT_ROW(1, 0, 0, 1, 0),
             FONT_ROW(1, 0, 0, 0, 1) } },
    { 'S', { FONT_ROW(0, 1, 1, 1, 1), FONT_ROW(1, 0, 0, 0, 0), FONT_ROW(1, 0, 0, 0, 0),
             FONT_ROW(0, 1, 1, 1, 0), FONT_ROW(0, 0, 0, 0, 1), FONT_ROW(0, 0, 0, 0, 1),
             FONT_ROW(1, 1, 1, 1, 0) } },
    { 'T', { FONT_ROW(1, 1, 1, 1, 1), FONT_ROW(0, 0, 1, 0, 0), FONT_ROW(0, 0, 1, 0, 0),
             FONT_ROW(0, 0, 1, 0, 0), FONT_ROW(0, 0, 1, 0, 0), FONT_ROW(0, 0, 1, 0, 0),
             FONT_ROW(0, 0, 1, 0, 0) } },
    { 'U', { FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1),
             FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1),
             FONT_ROW(0, 1, 1, 1, 0) } },
    { 'V', { FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1),
             FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(0, 1, 0, 1, 0),
             FONT_ROW(0, 0, 1, 0, 0) } },
    { 'W', { FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1),
             FONT_ROW(1, 0, 1, 0, 1), FONT_ROW(1, 0, 1, 0, 1), FONT_ROW(1, 1, 0, 1, 1),
             FONT_ROW(1, 0, 0, 0, 1) } },
    { 'X', { FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(0, 1, 0, 1, 0),
             FONT_ROW(0, 0, 1, 0, 0), FONT_ROW(0, 1, 0, 1, 0), FONT_ROW(1, 0, 0, 0, 1),
             FONT_ROW(1, 0, 0, 0, 1) } },
    { 'Y', { FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(0, 1, 0, 1, 0),
             FONT_ROW(0, 0, 1, 0, 0), FONT_ROW(0, 0, 1, 0, 0), FONT_ROW(0, 0, 1, 0, 0),
             FONT_ROW(0, 0, 1, 0, 0) } },
    { 'Z', { FONT_ROW(1, 1, 1, 1, 1), FONT_ROW(0, 0, 0, 0, 1), FONT_ROW(0, 0, 0, 1, 0),
             FONT_ROW(0, 0, 1, 0, 0), FONT_ROW(0, 1, 0, 0, 0), FONT_ROW(1, 0, 0, 0, 0),
             FONT_ROW(1, 1, 1, 1, 1) } },

    { '0', { FONT_ROW(0, 1, 1, 1, 0), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 1, 1),
             FONT_ROW(1, 0, 1, 0, 1), FONT_ROW(1, 1, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1),
             FONT_ROW(0, 1, 1, 1, 0) } },
    { '1', { FONT_ROW(0, 0, 1, 0, 0), FONT_ROW(0, 1, 1, 0, 0), FONT_ROW(0, 0, 1, 0, 0),
             FONT_ROW(0, 0, 1, 0, 0), FONT_ROW(0, 0, 1, 0, 0), FONT_ROW(0, 0, 1, 0, 0),
             FONT_ROW(0, 1, 1, 1, 0) } },
    { '2', { FONT_ROW(0, 1, 1, 1, 0), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(0, 0, 0, 0, 1),
             FONT_ROW(0, 0, 0, 1, 0), FONT_ROW(0, 0, 1, 0, 0), FONT_ROW(0, 1, 0, 0, 0),
             FONT_ROW(1, 1, 1, 1, 1) } },
    { '3', { FONT_ROW(1, 1, 1, 1, 1), FONT_ROW(0, 0, 0, 1, 0), FONT_ROW(0, 0, 1, 0, 0),
             FONT_ROW(0, 0, 0, 1, 0), FONT_ROW(0, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1),
             FONT_ROW(0, 1, 1, 1, 0) } },
    { '4', { FONT_ROW(0, 0, 0, 1, 0), FONT_ROW(0, 0, 1, 1, 0), FONT_ROW(0, 1, 0, 1, 0),
             FONT_ROW(1, 0, 0, 1, 0), FONT_ROW(1, 1, 1, 1, 1), FONT_ROW(0, 0, 0, 1, 0),
             FONT_ROW(0, 0, 0, 1, 0) } },
    { '5', { FONT_ROW(1, 1, 1, 1, 1), FONT_ROW(1, 0, 0, 0, 0), FONT_ROW(1, 1, 1, 1, 0),
             FONT_ROW(0, 0, 0, 0, 1), FONT_ROW(0, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1),
             FONT_ROW(0, 1, 1, 1, 0) } },
    { '6', { FONT_ROW(0, 0, 1, 1, 0), FONT_ROW(0, 1, 0, 0, 0), FONT_ROW(1, 0, 0, 0, 0),
             FONT_ROW(1, 1, 1, 1, 0), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1),
             FONT_ROW(0, 1, 1, 1, 0) } },
    { '7', { FONT_ROW(1, 1, 1, 1, 1), FONT_ROW(0, 0, 0, 0, 1), FONT_ROW(0, 0, 0, 1, 0),
             FONT_ROW(0, 0, 1, 0, 0), FONT_ROW(0, 1, 0, 0, 0), FONT_ROW(0, 1, 0, 0, 0),
             FONT_ROW(0, 1, 0, 0, 0) } },
    { '8', { FONT_ROW(0, 1, 1, 1, 0), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1),
             FONT_ROW(0, 1, 1, 1, 0), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1),
             FONT_ROW(0, 1, 1, 1, 0) } },
    { '9', { FONT_ROW(0, 1, 1, 1, 0), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(1, 0, 0, 0, 1),
             FONT_ROW(0, 1, 1, 1, 1), FONT_ROW(0, 0, 0, 0, 1), FONT_ROW(0, 0, 0, 1, 0),
             FONT_ROW(0, 1, 1, 0, 0) } },

    { ':', { FONT_ROW(0, 0, 0, 0, 0), FONT_ROW(0, 0, 1, 0, 0), FONT_ROW(0, 0, 1, 0, 0),
             FONT_ROW(0, 0, 0, 0, 0), FONT_ROW(0, 0, 1, 0, 0), FONT_ROW(0, 0, 1, 0, 0),
             FONT_ROW(0, 0, 0, 0, 0) } },
    { '.', { FONT_ROW(0, 0, 0, 0, 0), FONT_ROW(0, 0, 0, 0, 0), FONT_ROW(0, 0, 0, 0, 0),
             FONT_ROW(0, 0, 0, 0, 0), FONT_ROW(0, 0, 0, 0, 0), FONT_ROW(0, 0, 1, 1, 0),
             FONT_ROW(0, 0, 1, 1, 0) } },
    { '-', { FONT_ROW(0, 0, 0, 0, 0), FONT_ROW(0, 0, 0, 0, 0), FONT_ROW(0, 0, 0, 0, 0),
             FONT_ROW(0, 1, 1, 1, 0), FONT_ROW(0, 0, 0, 0, 0), FONT_ROW(0, 0, 0, 0, 0),
             FONT_ROW(0, 0, 0, 0, 0) } },
    { '!', { FONT_ROW(0, 0, 1, 0, 0), FONT_ROW(0, 0, 1, 0, 0), FONT_ROW(0, 0, 1, 0, 0),
             FONT_ROW(0, 0, 1, 0, 0), FONT_ROW(0, 0, 1, 0, 0), FONT_ROW(0, 0, 0, 0, 0),
             FONT_ROW(0, 0, 1, 0, 0) } },
    { '?', { FONT_ROW(0, 1, 1, 1, 0), FONT_ROW(1, 0, 0, 0, 1), FONT_ROW(0, 0, 0, 0, 1),
             FONT_ROW(0, 0, 0, 1, 0), FONT_ROW(0, 0, 1, 0, 0), FONT_ROW(0, 0, 0, 0, 0),
             FONT_ROW(0, 0, 1, 0, 0) } },
    { '<', { FONT_ROW(0, 0, 0, 1, 0), FONT_ROW(0, 0, 1, 0, 0), FONT_ROW(0, 1, 0, 0, 0),
             FONT_ROW(1, 0, 0, 0, 0), FONT_ROW(0, 1, 0, 0, 0), FONT_ROW(0, 0, 1, 0, 0),
             FONT_ROW(0, 0, 0, 1, 0) } },
    { '>', { FONT_ROW(0, 1, 0, 0, 0), FONT_ROW(0, 0, 1, 0, 0), FONT_ROW(0, 0, 0, 1, 0),
             FONT_ROW(0, 0, 0, 0, 1), FONT_ROW(0, 0, 0, 1, 0), FONT_ROW(0, 0, 1, 0, 0),
             FONT_ROW(0, 1, 0, 0, 0) } },
};

const uint8_t *font_glyph(char c)
{
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }

    for (int i = 0; i < (int)(sizeof FONT_ENTRIES / sizeof FONT_ENTRIES[0]); ++i) {
        if (FONT_ENTRIES[i].c == c) {
            return FONT_ENTRIES[i].rows;
        }
    }
    return NULL;
}

int font_text_width(const char *text, int pixel)
{
    if (text == NULL || pixel < 1) {
        return 0;
    }

    const int glyphs = (int)strlen(text);
    if (glyphs == 0) {
        return 0;
    }

    /* n glyphs and n - 1 one-pixel gaps. */
    return glyphs * FONT_GLYPH_W * pixel + (glyphs - 1) * FONT_GLYPH_SPACING * pixel;
}
