#pragma once

#include <stdint.h>

#include "core/board.h"
#include "core/game.h"

/*
 * Presentation-only effects state ("juice"): particles, short-lived effect
 * timers and a screen shake. Deliberately free of SDL, OpenGL, allocation and
 * the clock, so the tests can drive it tick by tick and a frame can be replayed
 * exactly.
 *
 * Effects live in board cell coordinates: x to the right, y up, one unit per
 * board cell, the same convention as the board (row 0 is the bottom row). The
 * view turns them into pixels with the layout, so nothing here depends on the
 * window size.
 *
 * The game's rng_state is never touched: Fx keeps its own splitmix64 state,
 * seeded separately, so the piece order stays exactly what the seed says.
 *
 * The struct is fixed-size and malloc-free. There is no way to allocate on the
 * fly: when the particle pool is full, new spawns are dropped instead of
 * overwriting older ones. Dropping keeps an effect that is already on screen
 * intact, which reads better than a particle vanishing mid-flight, and it makes
 * the pool contents independent of how many spawns were refused.
 */

/* Fixed capacities; all three are generous for the events that feed them. */
#define FX_MAX_PARTICLES 1024
#define FX_MAX_TRAILS 8
#define FX_MAX_FLASH_CELLS 4

/* Longest integration step a single fx_update may take, in seconds. */
#define FX_MAX_DT 0.1f

/* Effect durations, in seconds. */
#define FX_LOCK_FLASH_SECONDS 0.15f
#define FX_TRAIL_SECONDS 0.22f
#define FX_LEVEL_PULSE_SECONDS 0.50f

/* White-ish lock flash: how much of the cell colour it covers at its peak. */
#define FX_LOCK_FLASH_STRENGTH 0.68f

/* A particle of the glow pool. Positions and velocities are in cells. */
typedef struct FxParticle {
    float x;
    float y;
    float vx;
    float vy;
    float life;     /* seconds left; <= 0 means the slot is free */
    float max_life;
    float size;     /* drawn edge length in cells */
    uint8_t color;  /* CellType, the colour index used by the board palette */
} FxParticle;

/* A vertical streak left by a hard-dropped piece, one per occupied column. */
typedef struct FxTrail {
    float x;     /* board column index; the view centres on x + 0.5 */
    float y0;    /* bottom of the swept region, in cells */
    float y1;    /* top of the swept region, in cells */
    float width; /* cells */
    float life;
    float max_life;
    uint8_t color; /* CellType */
} FxTrail;

/* One cell of the locked piece, for the brief white flash. */
typedef struct FxFlashCell {
    int8_t x;
    int8_t y;
    uint8_t color; /* CellType, kept so the flash matches the piece */
} FxFlashCell;

/*
 * A decaying ring. The offset is a triangle wave rather than a sine so the fx
 * layer needs no libm: it starts at full amplitude along the kick direction,
 * swings the other way and fades linearly to exactly zero when the timer runs
 * out. A stronger kick replaces a weaker one; a weaker one is ignored.
 */
typedef struct FxShake {
    float amplitude; /* peak offset in cells */
    float direction_x;
    float direction_y;
    float duration;
    float remaining;
    float oscillator; /* 0..1 within the current wave, advanced by fx_update */
    float offset_x;   /* cells, current offset applied by the view */
    float offset_y;
} FxShake;

/*
 * The label for a notable clear: a T-spin, four lines, a back-to-back bonus, a
 * combo or a perfect clear. It keeps a copy of the lock's GameClearResult and
 * stays up FX_LABEL_SECONDS, fading out over the last FX_LABEL_FADE_SECONDS.
 */
#define FX_LABEL_SECONDS 1.6f
#define FX_LABEL_FADE_SECONDS 0.4f

/* The longest label is B2B, MINI, T-SPIN, the clear, COMBO n, PERFECT, CLEAR. */
#define FX_LABEL_MAX_LINES 7
#define FX_LABEL_LINE_SIZE 12

typedef struct FxLabel {
    GameClearResult clear;
    float life; /* seconds left; <= 0 means no label */
} FxLabel;

/* Animation keyframes, exported so the tests can name them instead of guessing. */
#define FX_CLEAR_FLASH_PEAK 0.20f
#define FX_CLEAR_FLASH_END 0.45f
#define FX_CLEAR_SWEEP_START 0.22f
#define FX_CLEAR_SWEEP_SPREAD 0.42f /* extra delay from the centre to the edge */
#define FX_CLEAR_SWEEP_SECONDS 0.30f /* time one column takes to shrink away */

/*
 * Result of the line-clear animation maths: how white the row is right now and
 * how much of each column is still visible. Both are pure functions of the
 * normalised clear progress, kept here (and unit tested) so the view stays a
 * thin drawing layer.
 */
typedef struct FxClearVisual {
    float progress;             /* clamped 0..1 */
    float flash;                /* 0..1 whiteness */
    float visible[BOARD_WIDTH]; /* 1 = full cell, 0 = gone */
} FxClearVisual;

typedef struct Fx {
    uint64_t seed;      /* kept so fx_reset restarts the same sequence */
    uint64_t rng_state; /* private splitmix64 state, never the game's */

    FxParticle particles[FX_MAX_PARTICLES];
    int particle_cursor;        /* where the next free-slot search starts */
    uint32_t particles_dropped; /* spawns refused because the pool was full */

    FxTrail trails[FX_MAX_TRAILS];
    int trail_cursor;

    FxFlashCell lock_cells[FX_MAX_FLASH_CELLS];
    int lock_cell_count;
    float lock_flash; /* seconds left */

    float level_pulse; /* seconds left of the board frame brightening */

    FxShake shake;

    FxLabel label;

    /* How strong the effects are, 0..1; see fx_set_intensity. */
    float intensity;
} Fx;

/* Clears all state, seeds the private generator and sets full intensity. */
void fx_init(Fx *fx, uint64_t seed);

/*
 * Clears every particle, trail, flash and timer and restarts the private
 * generator from the seed passed to fx_init, so a restart is reproducible. The
 * intensity is kept.
 */
void fx_reset(Fx *fx);

/*
 * Scales the effects, clamped to 0..1: the particle counts (rounded per cell),
 * the shake, the lock flash, the hard-drop trails and the level-up pulse. At 1
 * everything is exactly as tuned below; at 0 none of them happen. The
 * line-clear animation and the clear label are not scaled: they tell the player
 * what happened.
 */
void fx_set_intensity(Fx *fx, float intensity);

/*
 * Turns the events of a whole frame into effects. Call it once per frame after
 * the ticks have run and with the frame's OR-ed event bits; `game` supplies the
 * snapshots the core records (last_locked, last_hard_drop_*, last_clear), the
 * cleared rows and, while the clear delay runs, the cells that are about to
 * vanish.
 */
void fx_on_events(Fx *fx, uint32_t frame_events, const GameState *game);

/*
 * Advances every timer and integrates the particles by dt seconds. The step is
 * clamped to FX_MAX_DT so a stalled frame cannot fling particles across the
 * board or make the shake skip its whole lifetime. A non-positive dt does
 * nothing.
 */
void fx_update(Fx *fx, float dt);

/* Number of live particles (life > 0). */
int fx_particle_count(const Fx *fx);

/* How white the lock flash is right now: 0..1, scaled by the intensity. */
float fx_lock_flash_amount(const Fx *fx);

/* How bright the level-up frame pulse is right now: 0..1, scaled by the intensity. */
float fx_level_pulse_amount(const Fx *fx);

/* How visible the clear label is: 1 while it is up, fading to 0 at the end. */
float fx_label_alpha(const Fx *fx);

/*
 * The clear label's text, top line first, written into lines; returns how many
 * lines there are, 0 when no label is up. Lines are B2B, MINI, T-SPIN, the
 * clear (SINGLE, DOUBLE, TRIPLE or QUAD), COMBO n, and PERFECT and CLEAR, each
 * only when it applies.
 */
int fx_label_lines(const Fx *fx, char lines[][FX_LABEL_LINE_SIZE], int max_lines);

/* Current screen shake offset in board cells. Both are 0 when the shake is idle. */
void fx_shake_offset(const Fx *fx, float *out_x, float *out_y);

/*
 * Maps normalised line-clear progress (0 = lock, 1 = rows gone) to the flash
 * and per-column visibility used by board_view. Progress is clamped to 0..1.
 *
 * The flash rises to full over the first FX_CLEAR_FLASH_PEAK of the delay and
 * fades out by FX_CLEAR_FLASH_END. The cells shrink from the centre columns to
 * the edges between FX_CLEAR_SWEEP_START and the end, so every column's
 * visibility is monotonically non-increasing over time.
 */
void fx_clear_visual(float progress, FxClearVisual *out);

/* How the effects are tuned; the tests pin the count and shake scaling. */
#define FX_LINE_PARTICLES_PER_CELL 3
#define FX_LOCK_DUST_PER_CELL 2
#define FX_PARTICLE_GRAVITY (-9.0f) /* cells per second squared, downward */
#define FX_PARTICLE_DRAG 1.6f       /* first-order velocity damping per second */
#define FX_LINE_PARTICLE_LIFE_MIN 0.60f
#define FX_LINE_PARTICLE_LIFE_MAX 1.00f
#define FX_LINE_PARTICLE_SPEED_OUT 0.90f /* sideways cells/s per cell from centre */
#define FX_LINE_PARTICLE_SPEED_UP_MIN 4.00f
#define FX_LINE_PARTICLE_SPEED_UP_MAX 8.50f

#define FX_SHAKE_HARD_DROP_BASE 0.05f  /* cells, plus the per-row part */
#define FX_SHAKE_HARD_DROP_PER_ROW 0.02f
#define FX_SHAKE_HARD_DROP_SECONDS 0.30f
#define FX_SHAKE_LINE_BASE 0.06f /* cells, plus the per-line part */
#define FX_SHAKE_LINE_PER_ROW 0.04f
#define FX_SHAKE_LINE_SECONDS 0.36f
#define FX_SHAKE_MAX 0.25f /* cells; the hard drop cap */
#define FX_SHAKE_HZ 12.0f  /* wave cycles per second */
