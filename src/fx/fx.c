#include "fx/fx.h"

#include <stdio.h>
#include <string.h>

#include "core/piece.h"
#include "core/rng.h"

/*
 * A deterministic uniform float in [0, 1). splitmix64 gives us integers only, so
 * a fixed-resolution bounded draw is enough for jitter; 10000 steps is far finer
 * than anything visible.
 */
static float fx_random_unit(Fx *fx)
{
    return (float)rng_next_bounded(&fx->rng_state, 10000u) * (1.0f / 10000.0f);
}

static float fx_abs(float value)
{
    return value < 0.0f ? -value : value;
}

static float fx_clamp01(float value)
{
    if (value < 0.0f) {
        return 0.0f;
    }
    if (value > 1.0f) {
        return 1.0f;
    }
    return value;
}

static float fx_lerp(float from, float to, float t)
{
    return from + (to - from) * t;
}

static void fx_shake_trigger(Fx *fx, float amplitude, float direction_x, float direction_y,
                             float duration);

/*
 * Finds a free particle slot. The search starts where the previous spawn left
 * off, so live particles spread across the pool instead of piling up at the
 * front and ageing out together. A full pool refuses the spawn; see fx.h for why
 * dropping beats overwriting.
 */
/* A per-cell particle count scaled by the intensity, rounded to the nearest. */
static int fx_scaled_count(const Fx *fx, int count)
{
    return (int)((float)count * fx->intensity + 0.5f);
}

static FxParticle *fx_alloc_particle(Fx *fx)
{
    for (int i = 0; i < FX_MAX_PARTICLES; ++i) {
        const int index = (fx->particle_cursor + i) % FX_MAX_PARTICLES;
        if (fx->particles[index].life > 0.0f) {
            continue;
        }
        fx->particle_cursor = (index + 1) % FX_MAX_PARTICLES;
        FxParticle *particle = &fx->particles[index];
        memset(particle, 0, sizeof *particle);
        return particle;
    }
    ++fx->particles_dropped;
    return NULL;
}

static FxTrail *fx_alloc_trail(Fx *fx)
{
    for (int i = 0; i < FX_MAX_TRAILS; ++i) {
        const int index = (fx->trail_cursor + i) % FX_MAX_TRAILS;
        if (fx->trails[index].life > 0.0f) {
            continue;
        }
        fx->trail_cursor = (index + 1) % FX_MAX_TRAILS;
        FxTrail *trail = &fx->trails[index];
        memset(trail, 0, sizeof *trail);
        return trail;
    }
    return NULL;
}

static void fx_spawn_particle(Fx *fx, float x, float y, float vx, float vy, float life, float size,
                              CellType color)
{
    FxParticle *particle = fx_alloc_particle(fx);
    if (particle == NULL) {
        return;
    }
    particle->x = x;
    particle->y = y;
    particle->vx = vx;
    particle->vy = vy;
    particle->life = life;
    particle->max_life = life;
    particle->size = size;
    particle->color = (uint8_t)color;
}

/*
 * One burst per filled cell of every cleared row, in that cell's colour. The
 * rows are still on the board during the delay, which is what lets this read the
 * real cells instead of guessing from the piece snapshot.
 *
 * Sideways velocity points away from the board centre, so a row tears open
 * towards its edges; the upward launch plus gravity makes the parts arc and fall
 * back instead of hovering.
 */
static void fx_spawn_line_effects(Fx *fx, const GameState *game)
{
    const int rows = game->cleared_row_count > 4 ? 4 : game->cleared_row_count;

    for (int i = 0; i < rows; ++i) {
        const int y = game->cleared_rows[i];
        if (y >= BOARD_VISIBLE_HEIGHT) {
            continue;
        }

        for (int x = 0; x < BOARD_WIDTH; ++x) {
            const CellType type = board_get(&game->board, x, y);
            if (type == CELL_EMPTY) {
                continue;
            }

            const float centre_offset = ((float)x + 0.5f) - (float)BOARD_WIDTH * 0.5f;
            for (int n = 0; n < fx_scaled_count(fx, FX_LINE_PARTICLES_PER_CELL); ++n) {
                const float jitter = fx_random_unit(fx);
                const float side = fx_random_unit(fx);
                const float lift = fx_random_unit(fx);
                const float life = fx_random_unit(fx);

                fx_spawn_particle(
                    fx, (float)x + 0.5f + (side - 0.5f) * 0.5f,
                    (float)y + 0.5f + (jitter - 0.5f) * 0.5f,
                    centre_offset * FX_LINE_PARTICLE_SPEED_OUT + (side - 0.5f) * 2.0f,
                    fx_lerp(FX_LINE_PARTICLE_SPEED_UP_MIN, FX_LINE_PARTICLE_SPEED_UP_MAX, lift),
                    fx_lerp(FX_LINE_PARTICLE_LIFE_MIN, FX_LINE_PARTICLE_LIFE_MAX, life),
                    0.40f + jitter * 0.28f, type);
            }
        }
    }

    /* A tetris is four times the rows, so the shake scales with the row count. */
    const float direction_x = rng_next_bounded(&fx->rng_state, 2u) != 0 ? 0.35f : -0.35f;
    fx_shake_trigger(fx, FX_SHAKE_LINE_BASE + FX_SHAKE_LINE_PER_ROW * (float)rows, direction_x,
                     -0.94f, FX_SHAKE_LINE_SECONDS);
}

/*
 * Lock: a brief white flash over the four cells the piece just wrote, plus a
 * little dust under its bottom cells only, so the pieces that touch down with a
 * flat edge look like they kicked something up.
 */
static void fx_spawn_lock_effects(Fx *fx, const GameState *game)
{
    int xs[4];
    int ys[4];
    game_piece_cells(&game->last_locked, xs, ys);
    const CellType type = piece_cell_type(game->last_locked.type);

    fx->lock_cell_count = 0;
    int lowest = ys[0];
    for (int i = 1; i < 4; ++i) {
        if (ys[i] < lowest) {
            lowest = ys[i];
        }
    }

    for (int i = 0; i < 4; ++i) {
        fx->lock_cells[i] = (FxFlashCell){ (int8_t)xs[i], (int8_t)ys[i], (uint8_t)type };
        ++fx->lock_cell_count;

        if (ys[i] != lowest) {
            continue;
        }
        for (int n = 0; n < fx_scaled_count(fx, FX_LOCK_DUST_PER_CELL); ++n) {
            const float side = fx_random_unit(fx);
            const float fall = fx_random_unit(fx);
            fx_spawn_particle(fx, (float)xs[i] + 0.2f + side * 0.6f, (float)ys[i] - 0.05f,
                              (side - 0.5f) * 1.6f, -0.2f - fall * 1.2f, 0.25f + fall * 0.20f,
                              0.16f + side * 0.12f, type);
        }
    }
    fx->lock_flash = FX_LOCK_FLASH_SECONDS;
}

/*
 * Hard drop: one vertical trail per occupied column, from the landing cell up to
 * where the piece started, plus a downward kick whose size grows with the drop
 * distance and stops growing at FX_SHAKE_MAX.
 */
static void fx_spawn_hard_drop_effects(Fx *fx, const GameState *game)
{
    if (game->last_hard_drop_rows == 0 || fx->intensity <= 0.0f) {
        return;
    }

    int from_x[4];
    int from_y[4];
    int to_x[4];
    int to_y[4];
    game_piece_cells(&game->last_hard_drop_from, from_x, from_y);
    game_piece_cells(&game->last_locked, to_x, to_y);

    int columns[4];
    int column_count = 0;
    for (int i = 0; i < 4; ++i) {
        bool seen = false;
        for (int j = 0; j < column_count; ++j) {
            if (columns[j] == from_x[i]) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            columns[column_count++] = from_x[i];
        }
    }

    const CellType type = piece_cell_type(game->last_hard_drop_from.type);
    for (int c = 0; c < column_count; ++c) {
        float bottom = 0.0f;
        float top = 0.0f;
        bool have_bottom = false;
        bool have_top = false;

        for (int i = 0; i < 4; ++i) {
            if (from_x[i] != columns[c]) {
                continue;
            }
            const float cell_top = (float)from_y[i] + 1.0f;
            if (!have_top || cell_top > top) {
                top = cell_top;
                have_top = true;
            }
            if (!have_bottom || (float)to_y[i] < bottom) {
                bottom = (float)to_y[i];
                have_bottom = true;
            }
        }

        if (!have_top || !have_bottom || top <= bottom) {
            continue;
        }

        FxTrail *trail = fx_alloc_trail(fx);
        if (trail == NULL) {
            continue;
        }
        trail->x = (float)columns[c];
        trail->y0 = bottom;
        trail->y1 = top;
        trail->width = 0.32f;
        trail->life = FX_TRAIL_SECONDS;
        trail->max_life = FX_TRAIL_SECONDS;
        trail->color = (uint8_t)type;
    }

    float amplitude =
        FX_SHAKE_HARD_DROP_BASE + (float)game->last_hard_drop_rows * FX_SHAKE_HARD_DROP_PER_ROW;
    if (amplitude > FX_SHAKE_MAX) {
        amplitude = FX_SHAKE_MAX;
    }
    fx_shake_trigger(fx, amplitude, 0.0f, -1.0f, FX_SHAKE_HARD_DROP_SECONDS);
}

/*
 * Replaces the running shake when the new kick is at least as strong as what is
 * left of the old one, so a small lock does not cut off a tetris shake.
 */
static void fx_shake_trigger(Fx *fx, float amplitude, float direction_x, float direction_y,
                             float duration)
{
    amplitude *= fx->intensity;
    if (amplitude <= 0.0f || duration <= 0.0f) {
        return;
    }

    float running = 0.0f;
    if (fx->shake.remaining > 0.0f && fx->shake.duration > 0.0f) {
        running = fx->shake.amplitude * (fx->shake.remaining / fx->shake.duration);
    }
    if (amplitude < running) {
        return;
    }

    fx->shake.amplitude = amplitude;
    fx->shake.direction_x = direction_x;
    fx->shake.direction_y = direction_y;
    fx->shake.duration = duration;
    fx->shake.remaining = duration;
    fx->shake.oscillator = 0.0f;
    /* The wave starts at +1, so the kick is visible on the very next frame. */
    fx->shake.offset_x = amplitude * direction_x;
    fx->shake.offset_y = amplitude * direction_y;
}

void fx_init(Fx *fx, uint64_t seed)
{
    memset(fx, 0, sizeof *fx);
    fx->seed = seed;
    rng_seed(&fx->rng_state, seed);
    fx->intensity = 1.0f;
}

void fx_reset(Fx *fx)
{
    const uint64_t seed = fx->seed;
    const float intensity = fx->intensity;
    memset(fx, 0, sizeof *fx);
    fx->seed = seed;
    rng_seed(&fx->rng_state, seed);
    fx->intensity = intensity;
}

void fx_set_intensity(Fx *fx, float intensity)
{
    fx->intensity = fx_clamp01(intensity);
}

/* A clear worth a label: anything beyond a plain single, double or triple. */
static bool fx_clear_is_notable(const GameClearResult *clear)
{
    return clear->tspin != GAME_TSPIN_NONE || clear->lines >= 4 || clear->back_to_back ||
           clear->combo > 0 || clear->perfect_clear;
}

void fx_on_events(Fx *fx, uint32_t frame_events, const GameState *game)
{
    /*
     * The stack changes on every lock, so a fall in progress ends there. A
     * frame that holds both bits locked after the rows were removed (a clear
     * takes GAME_LINE_CLEAR_DELAY_TICKS to finish), so it starts no fall.
     */
    if ((frame_events & GAME_EVENT_LOCK) != 0) {
        fx->row_drop.life = 0.0f;
    } else if ((frame_events & GAME_EVENT_CLEAR_DONE) != 0 && game->cleared_row_count > 0) {
        const int count = game->cleared_row_count > 4 ? 4 : game->cleared_row_count;
        for (int i = 0; i < count; ++i) {
            fx->row_drop.rows[i] = game->cleared_rows[i];
        }
        fx->row_drop.count = (uint8_t)count;
        fx->row_drop.life = FX_ROW_DROP_SECONDS;
    }

    if ((frame_events & GAME_EVENT_LOCK) != 0 && fx_clear_is_notable(&game->last_clear)) {
        fx->label.clear = game->last_clear;
        fx->label.life = FX_LABEL_SECONDS;
    }
    if ((frame_events & GAME_EVENT_HARD_DROP) != 0) {
        fx_spawn_hard_drop_effects(fx, game);
    }
    if ((frame_events & GAME_EVENT_LOCK) != 0) {
        fx_spawn_lock_effects(fx, game);
    }
    if ((frame_events & GAME_EVENT_LINE_CLEAR) != 0) {
        fx_spawn_line_effects(fx, game);
    }
    if ((frame_events & GAME_EVENT_LEVEL_UP) != 0) {
        fx->level_pulse = FX_LEVEL_PULSE_SECONDS;
    }
}

void fx_update(Fx *fx, float dt)
{
    if (dt > 0.0f && fx->row_drop.life > 0.0f) {
        fx->row_drop.life -= dt > FX_MAX_DT ? FX_MAX_DT : dt;
        if (fx->row_drop.life < 0.0f) {
            fx->row_drop.life = 0.0f;
        }
    }
    if (dt > 0.0f && fx->label.life > 0.0f) {
        fx->label.life -= dt > FX_MAX_DT ? FX_MAX_DT : dt;
        if (fx->label.life < 0.0f) {
            fx->label.life = 0.0f;
        }
    }
    if (dt <= 0.0f) {
        return;
    }
    if (dt > FX_MAX_DT) {
        dt = FX_MAX_DT;
    }

    for (int i = 0; i < FX_MAX_PARTICLES; ++i) {
        FxParticle *particle = &fx->particles[i];
        if (particle->life <= 0.0f) {
            continue;
        }

        particle->vy += FX_PARTICLE_GRAVITY * dt;
        /* Stable first-order drag: a stall cannot make the velocity flip sign. */
        const float damping = 1.0f / (1.0f + FX_PARTICLE_DRAG * dt);
        particle->vx *= damping;
        particle->vy *= damping;
        particle->x += particle->vx * dt;
        particle->y += particle->vy * dt;

        particle->life -= dt;
        if (particle->life <= 0.0f) {
            particle->life = 0.0f;
        }
    }

    for (int i = 0; i < FX_MAX_TRAILS; ++i) {
        FxTrail *trail = &fx->trails[i];
        if (trail->life <= 0.0f) {
            continue;
        }
        trail->life -= dt;
        if (trail->life <= 0.0f) {
            trail->life = 0.0f;
        }
    }

    fx->lock_flash -= dt;
    if (fx->lock_flash < 0.0f) {
        fx->lock_flash = 0.0f;
    }
    fx->level_pulse -= dt;
    if (fx->level_pulse < 0.0f) {
        fx->level_pulse = 0.0f;
    }

    if (fx->shake.remaining > 0.0f) {
        fx->shake.remaining -= dt;
        if (fx->shake.remaining <= 0.0f) {
            fx->shake.remaining = 0.0f;
            fx->shake.amplitude = 0.0f;
            fx->shake.offset_x = 0.0f;
            fx->shake.offset_y = 0.0f;
            return;
        }

        fx->shake.oscillator += dt * FX_SHAKE_HZ;
        while (fx->shake.oscillator >= 1.0f) {
            fx->shake.oscillator -= 1.0f;
        }

        const float envelope = fx->shake.remaining / fx->shake.duration;
        /* Triangle wave: +1 at the start, -1 halfway through, +1 at the end. */
        const float wave = 4.0f * fx_abs(fx->shake.oscillator - 0.5f) - 1.0f;
        fx->shake.offset_x =
            fx->shake.amplitude * envelope * wave * fx->shake.direction_x;
        fx->shake.offset_y =
            fx->shake.amplitude * envelope * wave * fx->shake.direction_y;
    }
}

int fx_particle_count(const Fx *fx)
{
    int count = 0;
    for (int i = 0; i < FX_MAX_PARTICLES; ++i) {
        if (fx->particles[i].life > 0.0f) {
            ++count;
        }
    }
    return count;
}

float fx_lock_flash_amount(const Fx *fx)
{
    return fx_clamp01(fx->lock_flash / FX_LOCK_FLASH_SECONDS) * fx->intensity;
}

float fx_level_pulse_amount(const Fx *fx)
{
    return fx_clamp01(fx->level_pulse / FX_LEVEL_PULSE_SECONDS) * fx->intensity;
}

float fx_row_drop_offset(const Fx *fx, int row)
{
    if (fx->row_drop.life <= 0.0f) {
        return 0.0f;
    }

    /*
     * The rows were removed lowest first, so walking them in order finds how
     * many were under this row's old place: each one at or below it pushes the
     * old place up by one.
     */
    int distance = 0;
    for (int i = 0; i < fx->row_drop.count; ++i) {
        if ((int)fx->row_drop.rows[i] <= row + distance) {
            ++distance;
        }
    }
    if (distance == 0) {
        return 0.0f;
    }

    /* Starts at rest and speeds up: the distance left falls with time squared. */
    const float progress = 1.0f - fx_clamp01(fx->row_drop.life / FX_ROW_DROP_SECONDS);
    return (float)distance * (1.0f - progress * progress);
}

float fx_label_alpha(const Fx *fx)
{
    if (fx->label.life <= 0.0f) {
        return 0.0f;
    }
    return fx_clamp01(fx->label.life / FX_LABEL_FADE_SECONDS);
}

static int fx_label_add(char lines[][FX_LABEL_LINE_SIZE], int count, int max_lines,
                        const char *text)
{
    if (count >= max_lines) {
        return count;
    }
    snprintf(lines[count], FX_LABEL_LINE_SIZE, "%s", text);
    return count + 1;
}

int fx_label_lines(const Fx *fx, char lines[][FX_LABEL_LINE_SIZE], int max_lines)
{
    if (fx->label.life <= 0.0f) {
        return 0;
    }

    static const char *const CLEAR_NAMES[5] = { "", "SINGLE", "DOUBLE", "TRIPLE", "QUAD" };
    const GameClearResult *clear = &fx->label.clear;
    int count = 0;

    if (clear->back_to_back) {
        count = fx_label_add(lines, count, max_lines, "B2B");
    }
    if (clear->tspin == GAME_TSPIN_MINI) {
        count = fx_label_add(lines, count, max_lines, "MINI");
    }
    if (clear->tspin != GAME_TSPIN_NONE) {
        count = fx_label_add(lines, count, max_lines, "T-SPIN");
    }
    if (clear->lines > 0) {
        const int name = clear->lines > 4 ? 4 : clear->lines;
        count = fx_label_add(lines, count, max_lines, CLEAR_NAMES[name]);
    }
    if (clear->combo > 0) {
        char combo[FX_LABEL_LINE_SIZE];
        snprintf(combo, sizeof combo, "COMBO %u", (unsigned)clear->combo);
        count = fx_label_add(lines, count, max_lines, combo);
    }
    if (clear->perfect_clear) {
        count = fx_label_add(lines, count, max_lines, "PERFECT");
        count = fx_label_add(lines, count, max_lines, "CLEAR");
    }
    return count;
}

void fx_shake_offset(const Fx *fx, float *out_x, float *out_y)
{
    *out_x = fx->shake.offset_x;
    *out_y = fx->shake.offset_y;
}

void fx_clear_visual(float progress, FxClearVisual *out)
{
    if (progress < 0.0f) {
        progress = 0.0f;
    } else if (progress > 1.0f) {
        progress = 1.0f;
    }

    out->progress = progress;

    if (progress <= FX_CLEAR_FLASH_PEAK) {
        out->flash = progress / FX_CLEAR_FLASH_PEAK;
    } else {
        out->flash = 1.0f - (progress - FX_CLEAR_FLASH_PEAK) /
                                (FX_CLEAR_FLASH_END - FX_CLEAR_FLASH_PEAK);
        out->flash = fx_clamp01(out->flash);
    }

    const float centre = (float)(BOARD_WIDTH - 1) * 0.5f;
    for (int x = 0; x < BOARD_WIDTH; ++x) {
        const float distance = centre > 0.0f ? fx_abs((float)x - centre) / centre : 0.0f;
        const float start = FX_CLEAR_SWEEP_START + distance * FX_CLEAR_SWEEP_SPREAD;
        out->visible[x] = fx_clamp01(1.0f - (progress - start) / FX_CLEAR_SWEEP_SECONDS);
    }
}
