#include "render/fx_view.h"

#include "render/board_view.h"

/*
 * Cell inset and corner radius of the lock flash. They match
 * BOARD_VIEW_CELL_INSET in board_view.c and BLOCK_CORNER_RADIUS in quad.frag,
 * so the flash covers exactly the rounded block underneath; there is no shared
 * header for them because nothing else needs the values. The flash also glows
 * a little past the block.
 */
#define FX_VIEW_CELL_INSET 0.06f
#define FX_VIEW_FLASH_RADIUS 0.18f
#define FX_VIEW_FLASH_GLOW_CELLS 0.25f

/* Peak alpha of each element; the timers scale these down as they fade. */
#define FX_VIEW_PARTICLE_ALPHA 0.90f
#define FX_VIEW_TRAIL_ALPHA 0.55f
#define FX_VIEW_TRAIL_WIDE_ALPHA 0.18f
#define FX_VIEW_TRAIL_WIDE_SCALE 2.10f

/* How far outside the playfield particles may still draw, in cells. */
#define FX_VIEW_CLIP_MARGIN_CELLS 0.5f

static float fx_view_pixel_x(const BoardLayout *layout, float x)
{
    return layout->origin_x + x * (float)layout->cell_size;
}

static float fx_view_pixel_y(const BoardLayout *layout, float y)
{
    return layout->origin_y + y * (float)layout->cell_size;
}

static void fx_view_draw_lock_flash(Renderer *renderer, const BoardLayout *layout, const Fx *fx)
{
    const float amount = fx_lock_flash_amount(fx);
    if (amount <= 0.0f) {
        return;
    }

    const float cell = (float)layout->cell_size;
    const float inset = cell * FX_VIEW_CELL_INSET;
    const float inner = cell - 2.0f * inset;
    const float reach = cell * FX_VIEW_FLASH_GLOW_CELLS;
    const Color color = { 1.0f, 1.0f, 1.0f, amount * FX_LOCK_FLASH_STRENGTH };

    for (int i = 0; i < fx->lock_cell_count; ++i) {
        const FxFlashCell *flash = &fx->lock_cells[i];
        if (flash->y < 0 || flash->y >= BOARD_VISIBLE_HEIGHT) {
            continue;
        }
        renderer_push_styled(renderer, fx_view_pixel_x(layout, (float)flash->x) + inset - reach,
                             fx_view_pixel_y(layout, (float)flash->y) + inset - reach,
                             inner + 2.0f * reach, inner + 2.0f * reach, color, RECT_STYLE_NEON,
                             0.0f, reach, inner * FX_VIEW_FLASH_RADIUS);
    }
}

static void fx_view_draw_trails(Renderer *renderer, const BoardLayout *layout, const Fx *fx)
{
    const float cell = (float)layout->cell_size;

    for (int i = 0; i < FX_MAX_TRAILS; ++i) {
        const FxTrail *trail = &fx->trails[i];
        if (trail->life <= 0.0f || trail->max_life <= 0.0f) {
            continue;
        }

        const float fade = trail->life / trail->max_life;
        const Color base = board_view_cell_color((CellType)trail->color);
        const float centre_x = fx_view_pixel_x(layout, trail->x + 0.5f);
        const float bottom = fx_view_pixel_y(layout, trail->y0);
        const float height = (trail->y1 - trail->y0) * cell;
        if (height <= 0.0f) {
            continue;
        }

        /* A soft wide halo plus a brighter core reads as a streak rather than a
           solid bar without needing another shader style. */
        const float wide = trail->width * cell * FX_VIEW_TRAIL_WIDE_SCALE;
        const float narrow = trail->width * cell;
        const Color halo = { base.r, base.g, base.b, fade * FX_VIEW_TRAIL_WIDE_ALPHA };
        const Color core = { base.r, base.g, base.b, fade * FX_VIEW_TRAIL_ALPHA };

        renderer_push_rect(renderer, centre_x - wide * 0.5f, bottom, wide, height, halo);
        renderer_push_rect(renderer, centre_x - narrow * 0.5f, bottom, narrow, height, core);
    }
}

static void fx_view_draw_particles(Renderer *renderer, const BoardLayout *layout, const Fx *fx)
{
    const float cell = (float)layout->cell_size;

    for (int i = 0; i < FX_MAX_PARTICLES; ++i) {
        const FxParticle *particle = &fx->particles[i];
        if (particle->life <= 0.0f || particle->max_life <= 0.0f) {
            continue;
        }

        const float fade = particle->life / particle->max_life;
        const Color base = board_view_cell_color((CellType)particle->color);
        const float size = particle->size * cell;
        const Color color = { base.r, base.g, base.b, fade * FX_VIEW_PARTICLE_ALPHA };

        renderer_push_styled(renderer, fx_view_pixel_x(layout, particle->x) - size * 0.5f,
                             fx_view_pixel_y(layout, particle->y) - size * 0.5f, size, size,
                             color, RECT_STYLE_GLOW, 0.0f, 0.0f, 0.0f);
    }
}

static bool fx_view_has_trails(const Fx *fx)
{
    for (int i = 0; i < FX_MAX_TRAILS; ++i) {
        if (fx->trails[i].life > 0.0f) {
            return true;
        }
    }
    return false;
}

void fx_view_draw(Renderer *renderer, const BoardLayout *layout, const Fx *fx)
{
    const bool has_flash = fx_lock_flash_amount(fx) > 0.0f;
    const bool has_trails = fx_view_has_trails(fx);
    const bool has_particles = fx_particle_count(fx) > 0;
    if (!has_flash && !has_trails && !has_particles) {
        return;
    }

    if (has_flash) {
        fx_view_draw_lock_flash(renderer, layout, fx);
    }
    if (!has_trails && !has_particles) {
        return;
    }

    /* Glow adds up instead of covering the stack, so the board stays readable
       even in the middle of a tetris burst. */
    renderer_set_blend(renderer, RENDER_BLEND_ADDITIVE);
    renderer_set_clip(renderer, (int)(layout->origin_x -
                                      (float)layout->cell_size * FX_VIEW_CLIP_MARGIN_CELLS),
                      (int)(layout->origin_y -
                            (float)layout->cell_size * FX_VIEW_CLIP_MARGIN_CELLS),
                      layout->width + (int)((float)layout->cell_size * 2.0f *
                                            FX_VIEW_CLIP_MARGIN_CELLS),
                      layout->height + (int)((float)layout->cell_size * 2.0f *
                                             FX_VIEW_CLIP_MARGIN_CELLS));

    fx_view_draw_trails(renderer, layout, fx);
    fx_view_draw_particles(renderer, layout, fx);

    renderer_clear_clip(renderer);
    renderer_set_blend(renderer, RENDER_BLEND_ALPHA);
}
