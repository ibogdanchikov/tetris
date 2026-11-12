#include "render/menu_view.h"

#include <stdio.h>
#include <string.h>

#include <SDL3/SDL_stdinc.h>

#include "render/board_view.h"
#include "render/font.h"
#include "render/text.h"

/*
 * Same neon signage as the HUD: pink titles and a pink selection, cyan labels,
 * near-white values. Each glow color's alpha is its strength.
 */
static const Color MENU_PANEL_COLOR = { 0.010f, 0.012f, 0.025f, 0.95f };
static const Color MENU_DIM_COLOR = { 0.00f, 0.00f, 0.02f, 0.75f };
static const Color MENU_TITLE_COLOR = { 1.00f, 0.55f, 0.90f, 1.00f };
static const Color MENU_TITLE_GLOW = { 1.00f, 0.25f, 0.80f, 0.60f };
static const Color MENU_TEXT_COLOR = { 0.45f, 0.85f, 1.00f, 1.00f };
static const Color MENU_TEXT_GLOW = { 0.20f, 0.78f, 1.00f, 0.35f };
static const Color MENU_VALUE_COLOR = { 0.92f, 0.97f, 1.00f, 1.00f };
static const Color MENU_SELECTED_COLOR = { 1.00f, 0.60f, 0.92f, 1.00f };
static const Color MENU_SELECTED_GLOW = { 1.00f, 0.25f, 0.80f, 0.50f };
static const Color MENU_BAR_COLOR = { 1.00f, 0.30f, 0.85f, 0.80f };
static const Color MENU_BAR_FILL_COLOR = { 1.00f, 0.30f, 0.85f, 0.10f };
static const Color MENU_HINT_COLOR = { 0.45f, 0.85f, 1.00f, 0.75f };
static const Color MENU_HINT_GLOW = { 0.20f, 0.78f, 1.00f, 0.20f };

/* Widest row box on a full panel, in cells; value rows spread across it. */
#define MENU_ROW_BOX_CELLS 13

/* Row spacing as a multiple of the row text height, before and after squeezing. */
#define MENU_ROW_PITCH 2.0f
#define MENU_ROW_PITCH_TIGHT 1.5f

/* Spawn-orientation width of each piece in cells, for the pieces header. */
static const int MENU_PIECE_WIDTHS[PIECE_COUNT] = { 4, 2, 3, 3, 3, 3, 3 };

/* Cells across the pieces header: the pieces plus a one-cell gap between them. */
#define MENU_PIECES_HEADER_CELLS 27

/* A blinking hint is visible for this part of each period, in seconds. */
#define MENU_BLINK_PERIOD 1.2f
#define MENU_BLINK_ON 0.8f

static int menu_view_max(int a, int b)
{
    return a > b ? a : b;
}

static int menu_view_min(int a, int b)
{
    return a < b ? a : b;
}

/* The playfield and both panels: the area full-screen menus cover. */
static LayoutRect menu_view_group_rect(const BoardLayout *layout)
{
    const float left = layout->left_panel.x;
    const float right = layout->right_panel.x + layout->right_panel.w;
    return (LayoutRect){ left, layout->origin_y, right - left, (float)layout->height };
}

static LayoutRect menu_view_board_rect(const BoardLayout *layout)
{
    return (LayoutRect){ layout->origin_x, layout->origin_y, (float)layout->width,
                         (float)layout->height };
}

static void menu_view_draw_panel(Renderer *renderer, LayoutRect area, int cell_size)
{
    board_view_draw_neon_frame(renderer, area, cell_size, 0.0f);
    renderer_push_rect(renderer, area.x, area.y, area.w, area.h, MENU_PANEL_COLOR);
}

/*
 * The largest text size, up to preferred, at which `glyphs` characters fit in
 * `width` pixels. With the font atlas text can be any size, so it fills the
 * room exactly; the pixel font only has whole pixels, so the size is rounded
 * down for it. Never below 1.
 */
static float menu_view_fit(const Renderer *renderer, float width, int glyphs, float preferred)
{
    float size = preferred;
    if (glyphs > 0) {
        const float units =
            (float)(glyphs * (FONT_GLYPH_W + FONT_GLYPH_SPACING) - FONT_GLYPH_SPACING);
        const float fit = width / units;
        size = fit < size ? fit : size;
    }
    if (!renderer_has_font(renderer)) {
        size = (float)(int)size;
    }
    return size < 1.0f ? 1.0f : size;
}

/* One step smaller, when a page is too tall: a quarter unit with the atlas, a
   whole pixel with the pixel font. Never below 1. */
static float menu_view_shrink(const Renderer *renderer, float size)
{
    const float smaller = size - (renderer_has_font(renderer) ? 0.25f : 1.0f);
    return smaller < 1.0f ? 1.0f : smaller;
}

/* The neon bar behind a selected row or key slot. */
static void menu_view_draw_bar(Renderer *renderer, float x, float y, float w, float h,
                               float pixel)
{
    const float pad = 2.0f * pixel;
    const float reach = 2.0f * pixel;
    float tube = 0.6f * pixel;
    if (tube < 1.5f) {
        tube = 1.5f;
    }
    const float bx = x - pad;
    const float by = y - pad;
    const float bw = w + 2.0f * pad;
    const float bh = h + 2.0f * pad;

    renderer_set_blend(renderer, RENDER_BLEND_ADDITIVE);
    renderer_push_styled(renderer, bx - reach, by - reach, bw + 2.0f * reach, bh + 2.0f * reach,
                         MENU_BAR_FILL_COLOR, RECT_STYLE_NEON, 0.0f, reach, 2.0f * pixel);
    renderer_push_styled(renderer, bx - reach, by - reach, bw + 2.0f * reach, bh + 2.0f * reach,
                         MENU_BAR_COLOR, RECT_STYLE_NEON, tube, reach, 2.0f * pixel);
    renderer_set_blend(renderer, RENDER_BLEND_ALPHA);
}

/* Space between a title and what follows it. */
static float menu_view_title_gap(float pixel, int cell_size)
{
    const float gap = 3.0f * pixel;
    const float half_cell = (float)cell_size * 0.5f;
    return gap > half_cell ? gap : half_cell;
}

/* Text size for the hint line at the bottom of a panel. */
static float menu_view_hint_pixel(const Renderer *renderer, const char *hint, float width,
                                  int cell_size)
{
    return menu_view_fit(renderer, width, (int)strlen(hint), (float)(cell_size + 6) / 12.0f);
}

/* Width of a string at a text size; font_text_width for any size. */
static float menu_view_text_width(const char *text, float pixel)
{
    const int glyphs = (int)strlen(text);
    if (glyphs == 0) {
        return 0.0f;
    }
    return (float)(glyphs * (FONT_GLYPH_W + FONT_GLYPH_SPACING) - FONT_GLYPH_SPACING) * pixel;
}

static float menu_view_round(float value)
{
    return (float)(int)(value + (value >= 0.0f ? 0.5f : -0.5f));
}

static void menu_view_text_centered(Renderer *renderer, float x, float w, float y, float pixel,
                                    Color color, Color glow, const char *text)
{
    text_draw(renderer, menu_view_round(x + (w - menu_view_text_width(text, pixel)) * 0.5f), y,
              pixel, color, glow, text);
}

static bool menu_view_blink_on(float time_seconds)
{
    return SDL_fmodf(time_seconds, MENU_BLINK_PERIOD) < MENU_BLINK_ON;
}

/* --- the pieces header ---------------------------------------------------- */

static int menu_view_pieces_cell(const BoardLayout *layout, LayoutRect area)
{
    const int width = (int)area.w - 2 * layout->cell_size;
    return menu_view_max(2, menu_view_min(layout->cell_size,
                                          width / MENU_PIECES_HEADER_CELLS));
}

/* The seven pieces side by side, bobbing slightly out of step. Top at top. */
static void menu_view_draw_pieces(Renderer *renderer, LayoutRect area, int piece_cell,
                                  float top, float time_seconds)
{
    const float cell = (float)piece_cell;
    float x = area.x + (area.w - (float)MENU_PIECES_HEADER_CELLS * cell) * 0.5f;
    for (int type = 0; type < PIECE_COUNT; ++type) {
        const float w = (float)MENU_PIECE_WIDTHS[type] * cell;
        const float bob = SDL_sinf(time_seconds * 2.0f + (float)type * 0.9f) * cell * 0.25f;
        const LayoutRect box = { menu_view_round(x), menu_view_round(top - 2.0f * cell + bob), w,
                                 2.0f * cell };
        board_view_draw_preview(renderer, box, piece_cell, (PieceType)type,
                                board_view_cell_color(piece_cell_type((PieceType)type)));
        x += w + cell;
    }
}

/* --- menu pages ----------------------------------------------------------- */

/*
 * What a page holds, top to bottom: a header (a title or the pieces), info
 * lines, the rows, and a hint at the bottom of the area.
 */
typedef struct MenuViewPage {
    LayoutRect area;
    const char *title; /* NULL draws the pieces instead */
    const char *info[2];
    int info_count;
    const MenuItem *items;
    int item_count;
    const char *hint; /* NULL for none */
    bool hint_blinks;
    float row_box_width;
    bool table; /* value rows put the label left and the value right */
} MenuViewPage;

/* Text of a row as drawn: plain rows are the label, value rows get both. */
static int menu_view_row_glyphs(const MenuViewState *state, MenuItem item)
{
    int glyphs = (int)strlen(menu_item_label(item));
    if (menu_item_has_value(item)) {
        char value[32];
        menu_item_value(item, state->settings, value, sizeof value);
        glyphs += 2 + (int)strlen(value) + 4; /* a gap and the "< " " >" arrows */
    }
    return glyphs;
}

static void menu_view_draw_row(Renderer *renderer, const MenuViewState *state, MenuItem item,
                               bool selected, bool table, float x, float w, float y, float pixel)
{
    const float height = (float)FONT_GLYPH_H * pixel;
    if (selected) {
        menu_view_draw_bar(renderer, x, y, w, height, pixel);
    }
    const Color color = selected ? MENU_SELECTED_COLOR : MENU_TEXT_COLOR;
    const Color glow = selected ? MENU_SELECTED_GLOW : MENU_TEXT_GLOW;
    const char *label = menu_item_label(item);

    if (!menu_item_has_value(item)) {
        menu_view_text_centered(renderer, x, w, y, pixel, color, glow, label);
        return;
    }

    char value[32];
    menu_item_value(item, state->settings, value, sizeof value);
    char shown[40];
    snprintf(shown, sizeof shown, selected ? "< %s >" : "  %s  ", value);
    if (!table) {
        char row[80];
        snprintf(row, sizeof row, "%s %s", label, shown);
        menu_view_text_centered(renderer, x, w, y, pixel, color, glow, row);
        return;
    }
    text_draw(renderer, x, y, pixel, color, glow, label);
    text_draw(renderer, menu_view_round(x + w - menu_view_text_width(shown, pixel)), y, pixel,
              selected ? MENU_SELECTED_COLOR : MENU_VALUE_COLOR, glow, shown);
}

static void menu_view_draw_page(Renderer *renderer, const BoardLayout *layout,
                                const MenuViewState *state, const MenuViewPage *page)
{
    const int cell = layout->cell_size;
    const int inner = menu_view_max(1, (int)page->area.w - 2 * cell);
    const int box_w = menu_view_min(inner, (int)page->row_box_width);

    /* Header. */
    float title_pixel = 1.0f;
    int piece_cell = 0;
    float header_h = 0.0f;
    if (page->title != NULL) {
        title_pixel = menu_view_fit(renderer, (float)inner, (int)strlen(page->title),
                                    (float)cell / 4.0f);
        header_h = (float)FONT_GLYPH_H * title_pixel;
    } else {
        piece_cell = menu_view_pieces_cell(layout, page->area);
        header_h = 3.0f * (float)piece_cell;
    }

    /* Rows: the widest row decides the pixel size. */
    int widest = 1;
    for (int i = 0; i < page->item_count; ++i) {
        widest = menu_view_max(widest, menu_view_row_glyphs(state, page->items[i]));
    }
    for (int i = 0; i < page->info_count; ++i) {
        widest = menu_view_max(widest, (int)strlen(page->info[i]));
    }

    float pixel = menu_view_fit(renderer, (float)box_w, widest, (float)cell / 6.0f);
    float pitch = 0.0f;
    float total = 0.0f;
    float hint_pixel = 1.0f;
    for (;;) {
        const float row_h = (float)FONT_GLYPH_H * pixel;
        hint_pixel = page->hint == NULL
                         ? 1.0f
                         : menu_view_hint_pixel(renderer, page->hint, (float)inner, cell);
        const float gap = menu_view_title_gap(pixel, cell);
        const float fixed = header_h + gap + (float)page->info_count * row_h * 1.6f +
                            (page->info_count > 0 ? gap : 0.0f);
        const float room =
            page->area.h - (float)cell -
            (page->hint != NULL ? (float)FONT_GLYPH_H * hint_pixel + (float)cell : 0.0f);

        pitch = row_h * MENU_ROW_PITCH;
        total = fixed + (float)page->item_count * pitch;
        if (total > room) {
            pitch = row_h * MENU_ROW_PITCH_TIGHT;
            total = fixed + (float)page->item_count * pitch;
        }
        if (total <= room || pixel <= 1.0f) {
            break;
        }
        pixel = menu_view_shrink(renderer, pixel);
    }

    const float row_h = (float)FONT_GLYPH_H * pixel;
    const float gap = menu_view_title_gap(pixel, cell);
    const float hint_room =
        page->hint != NULL ? (float)FONT_GLYPH_H * hint_pixel + (float)cell : 0.0f;
    float top = page->area.y + hint_room + (page->area.h - hint_room + total) * 0.5f;

    if (page->title != NULL) {
        menu_view_text_centered(renderer, page->area.x, page->area.w, top - header_h,
                                title_pixel, MENU_TITLE_COLOR, MENU_TITLE_GLOW, page->title);
    } else {
        menu_view_draw_pieces(renderer, page->area, piece_cell, top - 0.25f * (float)piece_cell,
                              state->time_seconds);
    }
    top -= header_h + gap;

    for (int i = 0; i < page->info_count; ++i) {
        menu_view_text_centered(renderer, page->area.x, page->area.w, top - row_h, pixel,
                                MENU_VALUE_COLOR, MENU_TEXT_GLOW, page->info[i]);
        top -= row_h * 1.6f;
    }
    if (page->info_count > 0) {
        top -= gap;
    }

    const float box_x = menu_view_round(page->area.x + (page->area.w - (float)box_w) * 0.5f);
    for (int i = 0; i < page->item_count; ++i) {
        const float y = menu_view_round(top - row_h - (pitch - row_h) * 0.5f);
        menu_view_draw_row(renderer, state, page->items[i], i == state->menu->cursor,
                           page->table, box_x, (float)box_w, y, pixel);
        top -= pitch;
    }

    if (page->hint != NULL && (!page->hint_blinks || menu_view_blink_on(state->time_seconds))) {
        menu_view_text_centered(renderer, page->area.x, page->area.w,
                                page->area.y + (float)cell * 0.75f, hint_pixel,
                                MENU_HINT_COLOR, MENU_HINT_GLOW, page->hint);
    }
}

/* --- screens -------------------------------------------------------------- */

/* What a mode's table ranks by, as text: a time for Sprint, a score otherwise. */
static void menu_view_score_text(PlayMode mode, const HighScore *entry, char *out, size_t size)
{
    if (mode == PLAY_SPRINT) {
        menu_format_ticks(entry->ticks, out, size);
    } else {
        snprintf(out, size, "%lu", (unsigned long)entry->score);
    }
}

static void menu_view_draw_title(Renderer *renderer, const BoardLayout *layout,
                                 const MenuViewState *state)
{
    const LayoutRect area = menu_view_group_rect(layout);
    menu_view_draw_panel(renderer, area, layout->cell_size);

    /* The best run of the selected mode. */
    const PlayMode mode = (PlayMode)state->settings->mode;
    const ScoreTable *table = &state->save->tables[mode];
    char best[48] = "";
    if (table->count > 0) {
        char value[24];
        menu_view_score_text(mode, &table->entries[0], value, sizeof value);
        snprintf(best, sizeof best, "%s BEST %s", play_mode_name(mode), value);
    }

    MenuViewPage page;
    memset(&page, 0, sizeof page);
    page.area = area;
    page.title = NULL;
    page.info[0] = best;
    page.info_count = best[0] != '\0' ? 1 : 0;
    page.hint = "PRESS ENTER";
    page.hint_blinks = true;
    page.row_box_width = (float)(MENU_ROW_BOX_CELLS * layout->cell_size);
    menu_view_draw_page(renderer, layout, state, &page);
}

static void menu_view_draw_full_menu(Renderer *renderer, const BoardLayout *layout,
                                     const MenuViewState *state, const char *title,
                                     const char *hint, bool table)
{
    const LayoutRect area = menu_view_group_rect(layout);
    menu_view_draw_panel(renderer, area, layout->cell_size);

    MenuViewPage page;
    memset(&page, 0, sizeof page);
    page.area = area;
    page.title = title;
    page.item_count = menu_items(state->menu->screen, &page.items);
    page.hint = hint;
    page.row_box_width = (float)(MENU_ROW_BOX_CELLS * layout->cell_size);
    page.table = table;
    menu_view_draw_page(renderer, layout, state, &page);
}

static void menu_view_draw_board_menu(Renderer *renderer, const BoardLayout *layout,
                                      const MenuViewState *state, const char *title,
                                      const char *info0, const char *info1)
{
    const LayoutRect area = menu_view_board_rect(layout);
    renderer_push_rect(renderer, area.x, area.y, area.w, area.h, MENU_DIM_COLOR);

    MenuViewPage page;
    memset(&page, 0, sizeof page);
    page.area = area;
    page.title = title;
    if (info0 != NULL) {
        page.info[page.info_count++] = info0;
    }
    if (info1 != NULL) {
        page.info[page.info_count++] = info1;
    }
    page.item_count = menu_items(state->menu->screen, &page.items);
    page.row_box_width = area.w;
    menu_view_draw_page(renderer, layout, state, &page);
}

/*
 * The results of a run: CLEAR! with the time for a finished Sprint, TIME UP
 * with the score for Ultra, GAME OVER with the score for a top-out, and the
 * run's place in its table when it made one.
 */
static void menu_view_draw_game_over(Renderer *renderer, const BoardLayout *layout,
                                     const MenuViewState *state)
{
    const GameState *game = state->game;
    const char *title = "GAME OVER";
    char result[32];
    if (game->game_over_reason == GAME_OVER_LINE_GOAL) {
        title = "CLEAR!";
        char time[24];
        menu_format_ticks(game_run_ticks(game), time, sizeof time);
        snprintf(result, sizeof result, "TIME %s", time);
    } else {
        if (game->game_over_reason == GAME_OVER_TIME_UP) {
            title = "TIME UP";
        }
        snprintf(result, sizeof result, "SCORE %lu", (unsigned long)game->score);
    }

    char rank[32] = "";
    if (state->menu->last_rank == 0) {
        snprintf(rank, sizeof rank,
                 state->menu->last_mode == PLAY_SPRINT ? "NEW BEST TIME!" : "NEW HIGH SCORE!");
    } else if (state->menu->last_rank > 0) {
        snprintf(rank, sizeof rank, "RANK %d", state->menu->last_rank + 1);
    }
    menu_view_draw_board_menu(renderer, layout, state, title, result,
                              rank[0] != '\0' ? rank : NULL);
}

/*
 * The controls screen: one row per action with its two key slots in columns,
 * then the plain rows. Columns are 40/30/30 of the row box.
 */
static void menu_view_draw_controls(Renderer *renderer, const BoardLayout *layout,
                                    const MenuViewState *state)
{
    const LayoutRect area = menu_view_group_rect(layout);
    menu_view_draw_panel(renderer, area, layout->cell_size);

    const int cell = layout->cell_size;
    const int box_w = menu_view_max(1, (int)area.w - 2 * cell);
    const float label_w = (float)box_w * 0.40f;
    const float slot_w = (float)box_w * 0.30f;
    const Menu *menu = state->menu;
    const Settings *settings = state->settings;
    static const char WAITING[] = "PRESS KEY";

    const MenuItem *items = NULL;
    const int count = menu_items(SCREEN_CONTROLS, &items);

    /* The longest label and key name decide the pixel size. */
    int label_glyphs = 1;
    int slot_glyphs = (int)strlen(WAITING);
    for (int action = 0; action < BIND_ACTION_COUNT; ++action) {
        label_glyphs = menu_view_max(label_glyphs,
                                     (int)strlen(settings_action_name((BindAction)action)));
        for (int slot = 0; slot < SETTINGS_KEYS_PER_ACTION; ++slot) {
            slot_glyphs = menu_view_max(
                slot_glyphs, (int)strlen(state->key_name(settings->keys[action][slot])));
        }
    }
    const float preferred = (float)cell / 6.0f;
    const float label_pixel =
        menu_view_fit(renderer, label_w - (float)cell * 0.5f, label_glyphs, preferred);
    const float slot_pixel =
        menu_view_fit(renderer, slot_w - (float)cell * 0.5f, slot_glyphs, preferred);
    float pixel = label_pixel < slot_pixel ? label_pixel : slot_pixel;

    const char *hint = menu->waiting_for_key ? "PRESS A KEY   ESC CANCEL"
                                             : "ENTER SET   DEL CLEAR   ESC BACK";
    const float title_pixel = menu_view_fit(renderer, (float)box_w, 8, (float)cell / 4.0f);
    const float title_h = (float)FONT_GLYPH_H * title_pixel;
    const float hint_pixel = menu_view_hint_pixel(renderer, hint, (float)box_w, cell);
    float pitch = 0.0f;
    for (;;) {
        const float row_h = (float)FONT_GLYPH_H * pixel;
        const float room = area.h - (float)cell - title_h - menu_view_title_gap(pixel, cell) -
                           ((float)FONT_GLYPH_H * hint_pixel + (float)cell);
        pitch = row_h * MENU_ROW_PITCH;
        if ((float)count * pitch > room) {
            pitch = row_h * MENU_ROW_PITCH_TIGHT;
        }
        if ((float)count * pitch <= room || pixel <= 1.0f) {
            break;
        }
        pixel = menu_view_shrink(renderer, pixel);
    }

    const float row_h = (float)FONT_GLYPH_H * pixel;
    const float hint_room = (float)FONT_GLYPH_H * hint_pixel + (float)cell;
    const float title_gap = menu_view_title_gap(pixel, cell);
    const float total = title_h + title_gap + (float)count * pitch;
    float top = area.y + hint_room + (area.h - hint_room + total) * 0.5f;

    menu_view_text_centered(renderer, area.x, area.w, top - title_h, title_pixel,
                            MENU_TITLE_COLOR, MENU_TITLE_GLOW, "CONTROLS");
    top -= title_h + title_gap;

    const float box_x = menu_view_round(area.x + (area.w - (float)box_w) * 0.5f);
    for (int i = 0; i < count; ++i) {
        const float y = menu_view_round(top - row_h - (pitch - row_h) * 0.5f);
        const bool selected = i == menu->cursor;
        const int action = menu_item_bind_action(items[i]);

        if (action < 0) {
            menu_view_draw_row(renderer, state, items[i], selected, false, box_x,
                               (float)box_w, y, pixel);
            top -= pitch;
            continue;
        }

        text_draw(renderer, box_x, y, pixel, selected ? MENU_SELECTED_COLOR : MENU_TEXT_COLOR,
                  selected ? MENU_SELECTED_GLOW : MENU_TEXT_GLOW,
                  settings_action_name((BindAction)action));
        for (int slot = 0; slot < SETTINGS_KEYS_PER_ACTION; ++slot) {
            const float slot_x = box_x + label_w + (float)slot * slot_w;
            const float inner_w = slot_w - (float)cell * 0.5f;
            const bool slot_selected = selected && slot == menu->column;
            const char *name = slot_selected && menu->waiting_for_key
                                   ? WAITING
                                   : state->key_name(settings->keys[action][slot]);
            if (slot_selected) {
                menu_view_draw_bar(renderer, slot_x, y, inner_w, row_h, pixel);
            }
            menu_view_text_centered(renderer, slot_x, inner_w, y, pixel,
                                    slot_selected ? MENU_SELECTED_COLOR : MENU_VALUE_COLOR,
                                    slot_selected ? MENU_SELECTED_GLOW : MENU_TEXT_GLOW, name);
        }
        top -= pitch;
    }

    menu_view_text_centered(renderer, area.x, area.w, area.y + (float)cell * 0.75f, hint_pixel,
                            MENU_HINT_COLOR, MENU_HINT_GLOW, hint);
}

/*
 * One line of a score table; every line of a mode has the same length, so the
 * columns line up. Marathon shows score, level and lines; Sprint time and
 * lines; Ultra score and lines. A NULL entry is an empty place.
 */
static void menu_view_score_line(PlayMode mode, int place, const HighScore *entry, char *out,
                                 size_t size)
{
    char value[24] = "-";
    char lines[16] = "-";
    char level[16] = "-";
    if (entry != NULL) {
        menu_view_score_text(mode, entry, value, sizeof value);
        snprintf(lines, sizeof lines, "%lu", (unsigned long)entry->lines);
        snprintf(level, sizeof level, "%lu", (unsigned long)entry->level);
    }
    if (mode == PLAY_MARATHON) {
        snprintf(out, size, "%2d.  %10s  %3s  %5s", place, value, level, lines);
    } else {
        snprintf(out, size, "%2d.  %10s  %5s", place, value, lines);
    }
}

/* The score table of the mode menu->score_mode, LEFT and RIGHT switching modes. */
static void menu_view_draw_high_scores(Renderer *renderer, const BoardLayout *layout,
                                       const MenuViewState *state)
{
    const LayoutRect area = menu_view_group_rect(layout);
    menu_view_draw_panel(renderer, area, layout->cell_size);

    const int cell = layout->cell_size;
    const int inner = menu_view_max(1, (int)area.w - 2 * cell);
    const PlayMode mode = (PlayMode)state->menu->score_mode;
    const ScoreTable *table = &state->save->tables[mode];

    char lines[SAVE_MAX_SCORES + 1][48];
    if (mode == PLAY_MARATHON) {
        snprintf(lines[0], sizeof lines[0], "%3s  %10s  %3s  %5s", "", "SCORE", "LV", "LINES");
    } else {
        snprintf(lines[0], sizeof lines[0], "%3s  %10s  %5s", "",
                 mode == PLAY_SPRINT ? "TIME" : "SCORE", "LINES");
    }
    for (int i = 0; i < SAVE_MAX_SCORES; ++i) {
        menu_view_score_line(mode, i + 1, i < table->count ? &table->entries[i] : NULL,
                             lines[i + 1], sizeof lines[i + 1]);
    }
    char mode_line[32];
    snprintf(mode_line, sizeof mode_line, "< %s >", play_mode_name(mode));

    const char *title = "HIGH SCORES";
    const char *hint = "LEFT RIGHT MODE   ESC BACK";
    const float title_pixel =
        menu_view_fit(renderer, (float)inner, (int)strlen(title), (float)cell / 4.0f);
    const float title_h = (float)FONT_GLYPH_H * title_pixel;
    float pixel =
        menu_view_fit(renderer, (float)inner, (int)strlen(lines[0]), (float)cell / 6.0f);
    const float hint_pixel = menu_view_hint_pixel(renderer, hint, (float)inner, cell);
    const float hint_room = (float)FONT_GLYPH_H * hint_pixel + (float)cell;
    float pitch = 0.0f;
    for (;;) {
        pitch = (float)FONT_GLYPH_H * pixel * 1.7f;
        const float room =
            area.h - (float)cell - hint_room - title_h - menu_view_title_gap(pixel, cell);
        if ((float)(SAVE_MAX_SCORES + 2) * pitch <= room || pixel <= 1.0f) {
            break;
        }
        pixel = menu_view_shrink(renderer, pixel);
    }

    /* Title, the mode, the header and the ten places. */
    const float row_h = (float)FONT_GLYPH_H * pixel;
    const float title_gap = menu_view_title_gap(pixel, cell);
    const float total = title_h + title_gap + (float)(SAVE_MAX_SCORES + 2) * pitch;
    float top = area.y + hint_room + (area.h - hint_room + total) * 0.5f;
    menu_view_text_centered(renderer, area.x, area.w, top - title_h, title_pixel,
                            MENU_TITLE_COLOR, MENU_TITLE_GLOW, title);
    top -= title_h + title_gap;
    menu_view_text_centered(renderer, area.x, area.w, menu_view_round(top - row_h), pixel,
                            MENU_SELECTED_COLOR, MENU_SELECTED_GLOW, mode_line);
    top -= pitch;

    const float x = menu_view_round(area.x + (area.w - menu_view_text_width(lines[0], pixel)) *
                                                 0.5f);
    for (int i = 0; i <= SAVE_MAX_SCORES; ++i) {
        const bool header = i == 0;
        const bool latest =
            !header && i - 1 == state->menu->last_rank && (int)mode == state->menu->last_mode;
        const Color color = header ? MENU_TEXT_COLOR
                                   : (latest ? MENU_SELECTED_COLOR : MENU_VALUE_COLOR);
        text_draw(renderer, x, menu_view_round(top - row_h), pixel, color,
                  latest ? MENU_SELECTED_GLOW : MENU_TEXT_GLOW, lines[i]);
        top -= pitch;
    }

    menu_view_text_centered(renderer, area.x, area.w, area.y + (float)cell * 0.75f, hint_pixel,
                            MENU_HINT_COLOR, MENU_HINT_GLOW, hint);
}

bool menu_view_hides_game(const Menu *menu)
{
    switch (menu->screen) {
    case SCREEN_TITLE:
    case SCREEN_MAIN_MENU:
    case SCREEN_HIGH_SCORES:
    case SCREEN_SETTINGS:
    case SCREEN_CONTROLS:
        return true;
    case SCREEN_PLAYING:
    case SCREEN_PAUSED:
    case SCREEN_GAME_OVER:
    default:
        return false;
    }
}

void menu_view_draw(Renderer *renderer, const BoardLayout *layout, const MenuViewState *state)
{
    switch (state->menu->screen) {
    case SCREEN_TITLE:
        menu_view_draw_title(renderer, layout, state);
        break;
    case SCREEN_MAIN_MENU:
        menu_view_draw_full_menu(renderer, layout, state, NULL, "ARROWS MOVE   ENTER SELECT",
                                 false);
        break;
    case SCREEN_PAUSED:
        menu_view_draw_board_menu(renderer, layout, state, "PAUSED", NULL, NULL);
        break;
    case SCREEN_GAME_OVER:
        menu_view_draw_game_over(renderer, layout, state);
        break;
    case SCREEN_SETTINGS:
        menu_view_draw_full_menu(renderer, layout, state, "SETTINGS",
                                 "LEFT RIGHT CHANGE   ESC BACK", true);
        break;
    case SCREEN_CONTROLS:
        menu_view_draw_controls(renderer, layout, state);
        break;
    case SCREEN_HIGH_SCORES:
        menu_view_draw_high_scores(renderer, layout, state);
        break;
    case SCREEN_PLAYING:
    default:
        break;
    }
}
