#include "render/stroke_font.h"

#include <stddef.h>
#include <string.h>

#include "render/font.h"

/*
 * Glyph strokes. Each point is two digits, x then y, on the pixel font's grid:
 * x 0..4 from the left, y 0..6 from the bottom, so "06" is the top-left pixel.
 * Points in a stroke are joined by straight tubes; '|' starts a new stroke, and
 * a stroke of one repeated point is a dot. Corners are cut on the diagonal, the
 * way vector arcade lettering does it.
 */
typedef struct StrokeGlyph {
    char c;
    const char *strokes;
} StrokeGlyph;

static const StrokeGlyph STROKE_GLYPHS[] = {
    { 'A', "00 05 16 36 45 40|03 43" },
    { 'B', "00 06 36 45 44 33 03|33 42 41 30 00" },
    { 'C', "45 36 16 05 01 10 30 41" },
    { 'D', "00 06 36 45 41 30 00" },
    { 'E', "46 06 00 40|03 33" },
    { 'F', "46 06 00|03 33" },
    { 'G', "45 36 16 05 01 10 30 41 43 23" },
    { 'H', "00 06|40 46|03 43" },
    { 'I', "16 36|26 20|10 30" },
    { 'J', "16 46 41 30 10 01" },
    { 'K', "00 06|46 02|13 40" },
    { 'L', "06 00 40" },
    { 'M', "00 06 23 46 40" },
    { 'N', "00 06 40 46" },
    { 'O', "10 01 05 16 36 45 41 30 10" },
    { 'P', "00 06 36 45 44 33 03" },
    { 'Q', "10 01 05 16 36 45 41 30 10|22 40" },
    { 'R', "00 06 36 45 44 33 03|23 40" },
    { 'S', "45 36 16 05 04 13 33 42 41 30 10 01" },
    { 'T', "06 46|26 20" },
    { 'U', "06 01 10 30 41 46" },
    { 'V', "06 02 20 42 46" },
    { 'W', "06 00 23 40 46" },
    { 'X', "06 05 41 40|00 01 45 46" },
    { 'Y', "06 05 23 45 46|23 20" },
    { 'Z', "06 46 45 01 00 40" },
    { '0', "10 01 05 16 36 45 41 30 10|12 34" },
    { '1', "15 26 20|10 30" },
    { '2', "05 16 36 45 44 00 40" },
    { '3', "05 16 36 45 44 33 13|33 42 41 30 10 01" },
    { '4', "30 36 02 42" },
    { '5', "46 06 03 33 42 41 30 10 01" },
    { '6', "36 16 05 01 10 30 41 42 33 03" },
    { '7', "06 46 45 10" },
    { '8', "13 04 05 16 36 45 44 33 13 02 01 10 30 41 42 33" },
    { '9', "43 13 04 05 16 36 45 41 30 10" },
    { ':', "24 24|21 21" },
    { '.', "20 20" },
    { '-', "13 33" },
    { '!', "26 22|20 20" },
    { '?', "05 16 36 45 44 23 22|20 20" },
    { '<', "35 13 31" },
    { '>', "15 33 11" },
};

#define STROKE_GLYPH_COUNT ((int)(sizeof STROKE_GLYPHS / sizeof STROKE_GLYPHS[0]))

int stroke_font_glyph_index(char c)
{
    if (c >= 'a' && c <= 'z') {
        c = (char)(c - 'a' + 'A');
    }
    for (int i = 0; i < STROKE_GLYPH_COUNT; ++i) {
        if (STROKE_GLYPHS[i].c == c) {
            return i;
        }
    }
    return -1;
}

int stroke_font_glyph_count(void)
{
    return STROKE_GLYPH_COUNT;
}

void stroke_font_cell_size(int *out_width, int *out_height)
{
    *out_width = (int)(((float)FONT_GLYPH_W + 2.0f * STROKE_FONT_PAD) *
                       (float)STROKE_FONT_TEXELS_PER_UNIT);
    *out_height = (int)(((float)FONT_GLYPH_H + 2.0f * STROKE_FONT_PAD) *
                        (float)STROKE_FONT_TEXELS_PER_UNIT);
}

void stroke_font_atlas_size(int *out_width, int *out_height)
{
    int cell_width = 0;
    int cell_height = 0;
    stroke_font_cell_size(&cell_width, &cell_height);
    const int rows = (STROKE_GLYPH_COUNT + STROKE_FONT_COLUMNS - 1) / STROKE_FONT_COLUMNS;
    *out_width = cell_width * STROKE_FONT_COLUMNS;
    *out_height = cell_height * rows;
}

/* One stroke segment in glyph-box units, pixel centers at .5. */
typedef struct StrokeSegment {
    float ax;
    float ay;
    float bx;
    float by;
} StrokeSegment;

/* The most segments a glyph has; '8' is the longest at fifteen. */
#define STROKE_MAX_SEGMENTS 24

/* Turns a glyph's stroke string into segments. Returns how many. */
static int stroke_parse(const char *strokes, StrokeSegment out[STROKE_MAX_SEGMENTS])
{
    int count = 0;
    bool have_previous = false;
    float px = 0.0f;
    float py = 0.0f;

    for (const char *c = strokes; *c != '\0';) {
        if (*c == '|') {
            have_previous = false;
            ++c;
            continue;
        }
        if (*c == ' ') {
            ++c;
            continue;
        }
        if (c[1] == '\0') {
            break;
        }
        const float x = (float)(c[0] - '0') + 0.5f;
        const float y = (float)(c[1] - '0') + 0.5f;
        c += 2;

        /* Every point after a stroke's first joins the one before it. A point
           repeated makes a zero-length segment, which draws as a dot. */
        if (have_previous && count < STROKE_MAX_SEGMENTS) {
            out[count++] = (StrokeSegment){ px, py, x, y };
        }
        px = x;
        py = y;
        have_previous = true;
    }
    return count;
}

/* Squared distance from (x, y) to the segment, in units squared. */
static float stroke_distance_squared(const StrokeSegment *segment, float x, float y)
{
    const float dx = segment->bx - segment->ax;
    const float dy = segment->by - segment->ay;
    const float length_squared = dx * dx + dy * dy;
    float t = 0.0f;
    if (length_squared > 0.0f) {
        t = ((x - segment->ax) * dx + (y - segment->ay) * dy) / length_squared;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    }
    const float ex = x - (segment->ax + t * dx);
    const float ey = y - (segment->ay + t * dy);
    return ex * ex + ey * ey;
}

/* sqrt without libm: a few Newton steps from a good start are plenty here. */
static float stroke_sqrt(float value)
{
    if (value <= 0.0f) {
        return 0.0f;
    }
    float root = value > 1.0f ? value * 0.5f : 1.0f;
    for (int i = 0; i < 12; ++i) {
        root = 0.5f * (root + value / root);
    }
    return root;
}

bool stroke_font_build_atlas(unsigned char *pixels, int width, int height)
{
    int atlas_width = 0;
    int atlas_height = 0;
    stroke_font_atlas_size(&atlas_width, &atlas_height);
    if (pixels == NULL || width != atlas_width || height != atlas_height) {
        return false;
    }
    memset(pixels, 0, (size_t)width * (size_t)height);

    int cell_width = 0;
    int cell_height = 0;
    stroke_font_cell_size(&cell_width, &cell_height);
    const float texel = 1.0f / (float)STROKE_FONT_TEXELS_PER_UNIT;
    const float reach = STROKE_FONT_RADIUS + STROKE_FONT_SPREAD;

    for (int glyph = 0; glyph < STROKE_GLYPH_COUNT; ++glyph) {
        StrokeSegment segments[STROKE_MAX_SEGMENTS];
        const int segment_count = stroke_parse(STROKE_GLYPHS[glyph].strokes, segments);
        const int origin_x = (glyph % STROKE_FONT_COLUMNS) * cell_width;
        const int origin_y = (glyph / STROKE_FONT_COLUMNS) * cell_height;

        for (int ty = 0; ty < cell_height; ++ty) {
            const float y = ((float)ty + 0.5f) * texel - STROKE_FONT_PAD;
            for (int tx = 0; tx < cell_width; ++tx) {
                const float x = ((float)tx + 0.5f) * texel - STROKE_FONT_PAD;

                float nearest = reach * reach;
                for (int s = 0; s < segment_count; ++s) {
                    const float d = stroke_distance_squared(&segments[s], x, y);
                    nearest = d < nearest ? d : nearest;
                }
                const float distance = stroke_sqrt(nearest);
                float value = 0.5f + (STROKE_FONT_RADIUS - distance) / (2.0f * STROKE_FONT_SPREAD);
                value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
                pixels[(size_t)(origin_y + ty) * (size_t)width + (size_t)(origin_x + tx)] =
                    (unsigned char)(value * 255.0f + 0.5f);
            }
        }
    }
    return true;
}
