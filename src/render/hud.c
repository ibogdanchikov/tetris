#include "render/hud.h"

#include <stdio.h>
#include <string.h>

#include "render/board_view.h"
#include "render/font.h"
#include "render/text.h"

/*
 * Neon signage: cyan labels with a cyan glow, near-white values with the same
 * glow, and a pink title on the overlays. Each glow color's alpha is its
 * strength.
 */
static const Color HUD_BOX_COLOR = { 0.018f, 0.020f, 0.035f, 1.00f };
static const Color HUD_LABEL_COLOR = { 0.45f, 0.85f, 1.00f, 1.00f };
static const Color HUD_LABEL_GLOW = { 0.20f, 0.78f, 1.00f, 0.45f };
static const Color HUD_VALUE_COLOR = { 0.92f, 0.97f, 1.00f, 1.00f };
static const Color HUD_VALUE_GLOW = { 0.20f, 0.78f, 1.00f, 0.35f };
static const Color HUD_OVERLAY_COLOR = { 0.00f, 0.00f, 0.02f, 0.75f };
static const Color HUD_OVERLAY_TITLE_COLOR = { 1.00f, 0.55f, 0.90f, 1.00f };
static const Color HUD_OVERLAY_TITLE_GLOW = { 1.00f, 0.25f, 0.80f, 0.60f };
static const Color HUD_OVERLAY_HINT_COLOR = { 0.45f, 0.85f, 1.00f, 1.00f };
static const Color HUD_OVERLAY_HINT_GLOW = { 0.20f, 0.78f, 1.00f, 0.35f };

/* Empty text pixels between a stat label and its value. */
#define HUD_STAT_VALUE_GAP_PIXELS 3

/* Brightness of the grey used for a hold piece that is spent until the next lock. */
#define HUD_HOLD_GREY_FACTOR 0.75f

static int hud_padding(const BoardLayout *layout)
{
    const int pad = layout->cell_size / 8;
    return pad > 0 ? pad : 1;
}

/* A HUD box has the same neon frame as the playfield, without the pulse. */
static void hud_draw_box(Renderer *renderer, LayoutRect box, int cell_size)
{
    board_view_draw_neon_frame(renderer, box, cell_size, 0.0f);
    renderer_push_rect(renderer, box.x, box.y, box.w, box.h, HUD_BOX_COLOR);
}

/* Box content below its one-cell label band. */
static LayoutRect hud_box_content(LayoutRect box, int cell_size)
{
    return (LayoutRect){ box.x, box.y, box.w, box.h - (float)cell_size };
}

/* Desaturated and dim, for a hold piece that is spent until the next lock. */
static Color hud_greyed(Color color)
{
    const float luma = 0.30f * color.r + 0.59f * color.g + 0.11f * color.b;
    const float grey = luma * HUD_HOLD_GREY_FACTOR;
    return (Color){ grey, grey, grey, color.a };
}

static void hud_draw_hold(Renderer *renderer, const BoardLayout *layout, const GameState *game)
{
    hud_draw_box(renderer, layout->hold_box, layout->cell_size);
    text_draw_centered(renderer, layout->hold_box.x,
                       layout->hold_box.y + layout->hold_box.h - (float)layout->cell_size,
                       layout->hold_box.w, (float)layout->cell_size, layout->text_pixel,
                       HUD_LABEL_COLOR, HUD_LABEL_GLOW, "HOLD");

    if (!game->has_hold) {
        return;
    }

    Color color = board_view_cell_color(piece_cell_type(game->hold_piece));
    if (game->hold_used) {
        color = hud_greyed(color);
    }
    board_view_draw_preview(renderer, hud_box_content(layout->hold_box, layout->cell_size),
                            layout->preview_cell_size, game->hold_piece, color);
}

static void hud_draw_next(Renderer *renderer, const BoardLayout *layout, const GameState *game)
{
    hud_draw_box(renderer, layout->next_box, layout->cell_size);
    text_draw_centered(renderer, layout->next_box.x,
                       layout->next_box.y + layout->next_box.h - (float)layout->cell_size,
                       layout->next_box.w, (float)layout->cell_size, layout->text_pixel,
                       HUD_LABEL_COLOR, HUD_LABEL_GLOW, "NEXT");

    for (int i = 0; i < GAME_NEXT_COUNT; ++i) {
        const PieceType type = game->next_queue[i];
        const Color color = board_view_cell_color(piece_cell_type(type));
        board_view_draw_preview(renderer, layout->next_slots[i], layout->preview_cell_size, type,
                                color);
    }
}

/*
 * One label above one value, left-aligned. The value shrinks a pixel at a time
 * until it fits the panel, so a ten-digit score does not run over the board.
 */
static void hud_draw_stat(Renderer *renderer, const BoardLayout *layout, float x, float top,
                          const char *label, const char *value)
{
    const int label_pixel = layout->text_pixel;
    const float label_height = (float)(FONT_GLYPH_H * label_pixel);
    const float value_gap = (float)(HUD_STAT_VALUE_GAP_PIXELS * label_pixel);
    const int value_pixel = board_layout_fit_text_pixel(
        (int)layout->stats_area.w - 2 * hud_padding(layout), (int)strlen(value), label_pixel);
    const float value_height = (float)(FONT_GLYPH_H * value_pixel);

    text_draw(renderer, x, top - label_height, label_pixel, HUD_LABEL_COLOR, HUD_LABEL_GLOW,
              label);
    text_draw(renderer, x, top - label_height - value_gap - value_height, value_pixel,
              HUD_VALUE_COLOR, HUD_VALUE_GLOW, value);
}

static void hud_draw_stats(Renderer *renderer, const BoardLayout *layout, const GameState *game)
{
    char score[16];
    char level[16];
    char lines[16];
    snprintf(score, sizeof score, "%u", (unsigned)game->score);
    snprintf(level, sizeof level, "%u", (unsigned)game->level);
    snprintf(lines, sizeof lines, "%u", (unsigned)game->lines_cleared);

    const float pad = (float)hud_padding(layout);
    const float x = layout->stats_area.x + pad;
    const float line = (float)(FONT_GLYPH_H * layout->text_pixel);
    const float stride = 2.0f * line +
                         (float)(HUD_STAT_VALUE_GAP_PIXELS * layout->text_pixel) +
                         (float)layout->cell_size;

    float top = layout->stats_area.y + layout->stats_area.h - pad;
    hud_draw_stat(renderer, layout, x, top, "SCORE", score);
    top -= stride;
    hud_draw_stat(renderer, layout, x, top, "LEVEL", level);
    top -= stride;
    hud_draw_stat(renderer, layout, x, top, "LINES", lines);
}

static void hud_draw_overlay(Renderer *renderer, const BoardLayout *layout, const char *title,
                             const char *hint)
{
    renderer_push_rect(renderer, layout->origin_x, layout->origin_y, (float)layout->width,
                       (float)layout->height, HUD_OVERLAY_COLOR);

    const int title_pixel = board_layout_fit_text_pixel(
        layout->width - layout->cell_size, (int)strlen(title), layout->cell_size / 4);
    int hint_pixel = title_pixel / 2;
    if (hint_pixel < 1) {
        hint_pixel = 1;
    }

    const float title_height = (float)(FONT_GLYPH_H * title_pixel);
    const float hint_height = (float)(FONT_GLYPH_H * hint_pixel);
    const float gap = (float)title_pixel;
    const float block_height = title_height + gap + hint_height;
    const float bottom = layout->origin_y + ((float)layout->height - block_height) * 0.5f;

    text_draw_centered(renderer, layout->origin_x, bottom + hint_height + gap,
                       (float)layout->width, title_height, title_pixel, HUD_OVERLAY_TITLE_COLOR,
                       HUD_OVERLAY_TITLE_GLOW, title);
    text_draw_centered(renderer, layout->origin_x, bottom, (float)layout->width, hint_height,
                       hint_pixel, HUD_OVERLAY_HINT_COLOR, HUD_OVERLAY_HINT_GLOW, hint);
}

void hud_draw(Renderer *renderer, const BoardLayout *layout, const GameState *game, bool paused)
{
    hud_draw_hold(renderer, layout, game);
    hud_draw_next(renderer, layout, game);
    hud_draw_stats(renderer, layout, game);

    if (game->game_over) {
        hud_draw_overlay(renderer, layout, "GAME OVER", "PRESS R");
    } else if (paused) {
        hud_draw_overlay(renderer, layout, "PAUSED", "PRESS P");
    }
}
