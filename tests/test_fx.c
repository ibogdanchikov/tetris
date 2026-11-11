/*
 * Effects suites. The fx layer is deliberately GL-free and SDL-free, so these
 * tests drive it directly: no window, no renderer, no clock.
 */

#include <string.h>

#include "core/board.h"
#include "core/game.h"
#include "core/piece.h"
#include "fx/fx.h"
#include "test_harness.h"

static bool fx_near(float actual, float expected, float tolerance)
{
    float difference = actual - expected;
    if (difference < 0.0f) {
        difference = -difference;
    }
    return difference <= tolerance;
}

static void fx_fill_row(Board *board, int y, CellType type)
{
    for (int x = 0; x < BOARD_WIDTH; ++x) {
        board_set(board, x, y, type);
    }
}

/*
 * A board whose bottom `rows` rows are full and recorded as clearing. The cells
 * alternate colour so a test can tell which row a particle came from.
 */
static void fx_make_line_clear(GameState *game, int rows)
{
    board_clear(&game->board);
    game->cleared_row_count = (uint8_t)rows;
    for (int y = 0; y < rows; ++y) {
        fx_fill_row(&game->board, y, (y % 2 == 0) ? CELL_L : CELL_I);
        game->cleared_rows[y] = (uint8_t)y;
    }
    game->last_clear_count = (uint8_t)rows;
    game->phase = GAME_PHASE_CLEARING;
    game->clear_timer = GAME_LINE_CLEAR_DELAY_TICKS;
}

static void test_fx_init_and_reset(void)
{
    Fx fx;
    fx_init(&fx, 1234);

    CHECK_EQ_INT(fx_particle_count(&fx), 0);
    CHECK_EQ_INT(fx.particles_dropped, 0);
    CHECK(fx.lock_flash == 0.0f);
    CHECK_EQ_INT(fx.lock_cell_count, 0);
    CHECK(fx.level_pulse == 0.0f);
    CHECK(fx.shake.amplitude == 0.0f);

    float offset_x = 99.0f;
    float offset_y = 99.0f;
    fx_shake_offset(&fx, &offset_x, &offset_y);
    CHECK(offset_x == 0.0f);
    CHECK(offset_y == 0.0f);

    /* Fill the fx state, then reset it and check that nothing survived. */
    GameState game;
    game_init(&game, 1);
    fx_make_line_clear(&game, 2);
    game.last_locked = (ActivePiece){ PIECE_T, 0, 3, 5 };
    game.last_hard_drop_from = (ActivePiece){ PIECE_T, 0, 3, 15 };
    game.last_hard_drop_rows = 10;
    fx_on_events(&fx, GAME_EVENT_LINE_CLEAR | GAME_EVENT_LOCK | GAME_EVENT_HARD_DROP
                          | GAME_EVENT_LEVEL_UP,
                 &game);
    CHECK(fx_particle_count(&fx) > 0);
    CHECK(fx.lock_flash > 0.0f);
    CHECK(fx.level_pulse > 0.0f);
    CHECK(fx.shake.amplitude > 0.0f);

    fx_reset(&fx);
    CHECK_EQ_INT(fx_particle_count(&fx), 0);
    CHECK_EQ_INT(fx.particles_dropped, 0);
    CHECK(fx.lock_flash == 0.0f);
    CHECK_EQ_INT(fx.lock_cell_count, 0);
    CHECK(fx.level_pulse == 0.0f);
    CHECK(fx.shake.amplitude == 0.0f);
    CHECK(fx.shake.offset_x == 0.0f);
    CHECK(fx.shake.offset_y == 0.0f);
    for (int i = 0; i < FX_MAX_TRAILS; ++i) {
        CHECK(fx.trails[i].life == 0.0f);
    }

    /* The private generator restarts from the same seed, not from where it was. */
    Fx again;
    fx_init(&again, 1234);
    CHECK(memcmp(&fx.rng_state, &again.rng_state, sizeof fx.rng_state) == 0);
}

static void test_fx_line_clear_particles(void)
{
    Fx fx;
    Fx sparse;
    GameState game;
    game_init(&game, 1);
    const uint64_t game_rng_before = game.rng_state;

    fx_init(&fx, 7);
    fx_make_line_clear(&game, 1);
    fx_on_events(&fx, GAME_EVENT_LINE_CLEAR, &game);
    CHECK_EQ_INT(fx_particle_count(&fx),
                 BOARD_WIDTH * FX_LINE_PARTICLES_PER_CELL);
    const int single_count = fx_particle_count(&fx);
    const float single_shake = fx.shake.amplitude;

    /* Every particle sits in the cleared row, keeps that cell's colour and is
       inside the board horizontally. */
    for (int i = 0; i < FX_MAX_PARTICLES; ++i) {
        const FxParticle *particle = &fx.particles[i];
        if (particle->life <= 0.0f) {
            continue;
        }
        CHECK(particle->y >= 0.0f && particle->y < 1.0f);
        CHECK(particle->x >= 0.0f && particle->x <= (float)BOARD_WIDTH);
        CHECK_EQ_INT(particle->color, CELL_L);
        CHECK(particle->life >= FX_LINE_PARTICLE_LIFE_MIN);
        CHECK(particle->life <= FX_LINE_PARTICLE_LIFE_MAX);
    }

    /* Two rows use two colours and spawn twice as much. */
    fx_init(&sparse, 7);
    fx_make_line_clear(&game, 2);
    fx_on_events(&sparse, GAME_EVENT_LINE_CLEAR, &game);
    CHECK_EQ_INT(fx_particle_count(&sparse), 2 * single_count);
    for (int i = 0; i < FX_MAX_PARTICLES; ++i) {
        const FxParticle *particle = &sparse.particles[i];
        if (particle->life <= 0.0f) {
            continue;
        }
        const int row = (int)particle->y;
        CHECK(row == 0 || row == 1);
        CHECK_EQ_INT(particle->color, row == 0 ? CELL_L : CELL_I);
    }

    /* A tetris is bigger and shakes harder than a single. */
    Fx tetris;
    fx_init(&tetris, 7);
    fx_make_line_clear(&game, 4);
    fx_on_events(&tetris, GAME_EVENT_LINE_CLEAR, &game);
    CHECK_EQ_INT(fx_particle_count(&tetris), 4 * single_count);
    CHECK(tetris.shake.amplitude > single_shake);

    /* Effects have their own generator: the game's piece order is untouched. */
    CHECK(game.rng_state == game_rng_before);
}

static void test_fx_level_pulse(void)
{
    Fx fx;
    GameState game;
    game_init(&game, 10);
    fx_init(&fx, 15);

    fx_on_events(&fx, GAME_EVENT_LEVEL_UP, &game);
    CHECK(fx_near(fx_level_pulse_amount(&fx), 1.0f, 1e-6f));

    for (int i = 0; i < 40; ++i) {
        fx_update(&fx, 1.0f / 60.0f);
    }
    CHECK(fx_level_pulse_amount(&fx) == 0.0f);
}

static void test_fx_particles_expire(void)
{
    Fx fx;
    GameState game;
    game_init(&game, 2);
    fx_make_line_clear(&game, 4);

    fx_init(&fx, 3);
    fx_on_events(&fx, GAME_EVENT_LINE_CLEAR, &game);
    CHECK(fx_particle_count(&fx) > 0);

    /* Fixed 60 Hz steps for longer than the longest particle life. */
    const int steps = (int)(FX_LINE_PARTICLE_LIFE_MAX * 60.0f) + 10;
    for (int i = 0; i < steps; ++i) {
        fx_update(&fx, 1.0f / 60.0f);
    }
    CHECK_EQ_INT(fx_particle_count(&fx), 0);
}

static void test_fx_update_clamps_dt(void)
{
    Fx fx;
    GameState game;
    game_init(&game, 4);
    fx_make_line_clear(&game, 1);

    fx_init(&fx, 9);
    fx_on_events(&fx, GAME_EVENT_LINE_CLEAR, &game);

    const FxParticle *particle = NULL;
    for (int i = 0; i < FX_MAX_PARTICLES; ++i) {
        if (fx.particles[i].life > 0.0f) {
            particle = &fx.particles[i];
            break;
        }
    }
    CHECK(particle != NULL);
    if (particle == NULL) {
        return;
    }

    const float before = particle->life;
    fx_update(&fx, 10.0f); /* clamped to FX_MAX_DT */
    CHECK(fx_near(particle->life, before - FX_MAX_DT, 1e-5f));
}

static void test_fx_pool_capacity_and_dropping(void)
{
    Fx fx;
    GameState game;
    game_init(&game, 5);
    fx_make_line_clear(&game, 4);

    fx_init(&fx, 11);

    /* Far more spawns than the pool can hold: capacity is a hard bound. */
    for (int i = 0; i < 100; ++i) {
        fx_on_events(&fx, GAME_EVENT_LINE_CLEAR, &game);
        CHECK(fx_particle_count(&fx) <= FX_MAX_PARTICLES);
    }
    CHECK_EQ_INT(fx_particle_count(&fx), FX_MAX_PARTICLES);
    CHECK(fx.particles_dropped > 0);
}

static void test_fx_deterministic(void)
{
    GameState game;
    game_init(&game, 6);
    fx_make_line_clear(&game, 3);
    game.last_locked = (ActivePiece){ PIECE_T, 0, 3, 5 };
    game.last_hard_drop_from = (ActivePiece){ PIECE_T, 0, 3, 15 };
    game.last_hard_drop_rows = 10;

    Fx first;
    Fx second;
    fx_init(&first, 2024);
    fx_init(&second, 2024);

    for (int round = 0; round < 4; ++round) {
        const uint32_t events = GAME_EVENT_LINE_CLEAR | GAME_EVENT_LOCK | GAME_EVENT_HARD_DROP;
        fx_on_events(&first, events, &game);
        fx_on_events(&second, events, &game);
        fx_update(&first, 1.0f / 60.0f);
        fx_update(&second, 1.0f / 60.0f);
    }

    CHECK_EQ_INT(fx_particle_count(&first), fx_particle_count(&second));
    CHECK_EQ_INT(first.particles_dropped, second.particles_dropped);
    for (int i = 0; i < FX_MAX_PARTICLES; ++i) {
        const FxParticle *a = &first.particles[i];
        const FxParticle *b = &second.particles[i];
        CHECK(a->x == b->x);
        CHECK(a->y == b->y);
        CHECK(a->vx == b->vx);
        CHECK(a->vy == b->vy);
        CHECK(a->life == b->life);
        CHECK(a->max_life == b->max_life);
        CHECK(a->size == b->size);
        CHECK_EQ_INT(a->color, b->color);
    }
    for (int i = 0; i < FX_MAX_TRAILS; ++i) {
        CHECK(first.trails[i].x == second.trails[i].x);
        CHECK(first.trails[i].y0 == second.trails[i].y0);
        CHECK(first.trails[i].y1 == second.trails[i].y1);
        CHECK(first.trails[i].life == second.trails[i].life);
        CHECK_EQ_INT(first.trails[i].color, second.trails[i].color);
    }
    CHECK(first.shake.amplitude == second.shake.amplitude);
    CHECK(first.shake.offset_x == second.shake.offset_x);
    CHECK(first.shake.offset_y == second.shake.offset_y);
    CHECK_EQ_INT(first.lock_cell_count, second.lock_cell_count);
}

static void test_fx_lock_flash(void)
{
    Fx fx;
    GameState game;
    game_init(&game, 7);
    fx_init(&fx, 12);

    game.last_locked = (ActivePiece){ PIECE_T, 0, 3, 5 };
    fx_on_events(&fx, GAME_EVENT_LOCK, &game);

    CHECK_EQ_INT(fx.lock_cell_count, 4);
    CHECK(fx_near(fx.lock_flash, FX_LOCK_FLASH_SECONDS, 1e-6f));
    CHECK(fx_near(fx_lock_flash_amount(&fx), 1.0f, 1e-6f));
    /* Dust is born on the same event. */
    CHECK(fx_particle_count(&fx) > 0);

    int xs[4];
    int ys[4];
    game_piece_cells(&game.last_locked, xs, ys);
    for (int i = 0; i < 4; ++i) {
        CHECK_EQ_INT(fx.lock_cells[i].x, xs[i]);
        CHECK_EQ_INT(fx.lock_cells[i].y, ys[i]);
        CHECK_EQ_INT(fx.lock_cells[i].color, CELL_T);
    }

    /* The flash is short: after its duration the amount is exactly zero. */
    for (int i = 0; i < 20; ++i) {
        fx_update(&fx, 1.0f / 60.0f);
    }
    CHECK(fx.lock_flash == 0.0f);
    CHECK(fx_lock_flash_amount(&fx) == 0.0f);
}

static int fx_trail_count(const Fx *fx)
{
    int count = 0;
    for (int i = 0; i < FX_MAX_TRAILS; ++i) {
        if (fx->trails[i].life > 0.0f) {
            ++count;
        }
    }
    return count;
}

static void test_fx_hard_drop_trail_and_shake(void)
{
    Fx fx;
    GameState game;
    game_init(&game, 8);
    fx_init(&fx, 13);

    /* O drops 15 rows from y 15 to y 0; it is two wide, so two trails. */
    game.last_hard_drop_from = (ActivePiece){ PIECE_O, 0, 4, 15 };
    game.last_locked = (ActivePiece){ PIECE_O, 0, 4, 0 };
    game.last_hard_drop_rows = 15;
    fx_on_events(&fx, GAME_EVENT_HARD_DROP, &game);

    CHECK_EQ_INT(fx_trail_count(&fx), 2);
    for (int i = 0; i < FX_MAX_TRAILS; ++i) {
        const FxTrail *trail = &fx.trails[i];
        if (trail->life <= 0.0f) {
            continue;
        }
        CHECK(trail->x == 4.0f || trail->x == 5.0f);
        CHECK(fx_near(trail->y0, 0.0f, 1e-5f));
        /* The landing bottom up to the starting top: 15 rows plus the 2-tall O. */
        CHECK(fx_near(trail->y1, 17.0f, 1e-5f));
        CHECK(trail->y1 - trail->y0 > (float)game.last_hard_drop_rows);
        CHECK_EQ_INT(trail->color, CELL_O);
    }

    /* A hard drop pushes the board down on the first frame. */
    CHECK(fx.shake.amplitude > 0.0f);
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    fx_shake_offset(&fx, &offset_x, &offset_y);
    CHECK(fx_near(offset_y, -fx.shake.amplitude, 1e-6f));

    /* Trails are gone well within a second. */
    for (int i = 0; i < 60; ++i) {
        fx_update(&fx, 1.0f / 60.0f);
    }
    CHECK_EQ_INT(fx_trail_count(&fx), 0);

    /* The shake decays to exactly zero within its bounded lifetime. */
    for (int i = 0; i < 180; ++i) {
        fx_update(&fx, 1.0f / 60.0f);
    }
    CHECK(fx.shake.amplitude == 0.0f);
    fx_shake_offset(&fx, &offset_x, &offset_y);
    CHECK(offset_x == 0.0f);
    CHECK(offset_y == 0.0f);
}

static void test_fx_hard_drop_edges(void)
{
    Fx fx;
    GameState game;
    game_init(&game, 9);

    /* A piece that was already resting has no trail and no shake. */
    fx_init(&fx, 14);
    game.last_hard_drop_from = (ActivePiece){ PIECE_O, 0, 4, 0 };
    game.last_locked = (ActivePiece){ PIECE_O, 0, 4, 0 };
    game.last_hard_drop_rows = 0;
    fx_on_events(&fx, GAME_EVENT_HARD_DROP, &game);
    CHECK_EQ_INT(fx_trail_count(&fx), 0);
    CHECK(fx.shake.amplitude == 0.0f);

    /* The shake amplitude is capped however far the piece fell. */
    fx_init(&fx, 14);
    game.last_hard_drop_from = (ActivePiece){ PIECE_O, 0, 4, 100 };
    game.last_locked = (ActivePiece){ PIECE_O, 0, 4, 0 };
    game.last_hard_drop_rows = 100;
    fx_on_events(&fx, GAME_EVENT_HARD_DROP, &game);
    CHECK(fx.shake.amplitude <= FX_SHAKE_MAX);
    CHECK(fx_near(fx.shake.amplitude, FX_SHAKE_MAX, 1e-6f));
}

/* --- sparks -------------------------------------------------------------- */

static int fx_count_kind(const Fx *fx, FxParticleKind kind)
{
    int count = 0;
    for (int i = 0; i < FX_MAX_PARTICLES; ++i) {
        if (fx->particles[i].life > 0.0f && fx->particles[i].kind == kind) {
            ++count;
        }
    }
    return count;
}

/* A hard drop of `type` in spawn rotation from row 15 down to row 0 at x 3. */
static void fx_make_hard_drop(GameState *game, PieceType type, int rows)
{
    game->last_hard_drop_from = (ActivePiece){ type, 0, 3, (int8_t)rows };
    game->last_locked = (ActivePiece){ type, 0, 3, 0 };
    game->last_hard_drop_rows = (uint8_t)rows;
}

static void test_fx_hard_drop_sparks(void)
{
    Fx fx;
    GameState game;
    game_init(&game, 21);

    /* A flat T has three bottom cells (the top cell sits on the middle one),
       an O two, and each throws FX_SPARKS_PER_CELL sparks in the piece color. */
    fx_init(&fx, 22);
    fx_make_hard_drop(&game, PIECE_T, 15);
    fx_on_events(&fx, GAME_EVENT_HARD_DROP, &game);
    CHECK_EQ_INT(fx_count_kind(&fx, FX_PARTICLE_SPARK), 3 * FX_SPARKS_PER_CELL);
    CHECK_EQ_INT(fx_count_kind(&fx, FX_PARTICLE_GLOW), 0);

    int xs[4];
    int ys[4];
    game_piece_cells(&game.last_locked, xs, ys);
    int lowest = ys[0];
    for (int i = 1; i < 4; ++i) {
        lowest = ys[i] < lowest ? ys[i] : lowest;
    }
    const float bottom = (float)lowest;
    for (int i = 0; i < FX_MAX_PARTICLES; ++i) {
        const FxParticle *spark = &fx.particles[i];
        if (spark->life <= 0.0f) {
            continue;
        }
        CHECK_EQ_INT(spark->color, CELL_T);
        CHECK(spark->life >= FX_SPARK_LIFE_MIN && spark->life <= FX_SPARK_LIFE_MAX);
        CHECK(spark->vy > 0.0f); /* thrown up */
        CHECK(spark->x >= 3.0f && spark->x <= 6.0f);
        CHECK(spark->y >= bottom && spark->y < bottom + 0.5f);
    }

    fx_init(&fx, 22);
    fx_make_hard_drop(&game, PIECE_O, 15);
    fx_on_events(&fx, GAME_EVENT_HARD_DROP, &game);
    CHECK_EQ_INT(fx_count_kind(&fx, FX_PARTICLE_SPARK), 2 * FX_SPARKS_PER_CELL);

    /* No sparks from a piece that was already resting, or with the effects off. */
    fx_init(&fx, 22);
    fx_make_hard_drop(&game, PIECE_O, 0);
    fx_on_events(&fx, GAME_EVENT_HARD_DROP, &game);
    CHECK_EQ_INT(fx_particle_count(&fx), 0);
    fx_init(&fx, 22);
    fx_set_intensity(&fx, 0.0f);
    fx_make_hard_drop(&game, PIECE_O, 15);
    fx_on_events(&fx, GAME_EVENT_HARD_DROP, &game);
    CHECK_EQ_INT(fx_particle_count(&fx), 0);

    /* A short drop throws them slower than a long one. */
    Fx slow;
    Fx fast;
    fx_init(&slow, 23);
    fx_init(&fast, 23);
    fx_make_hard_drop(&game, PIECE_O, 1);
    fx_on_events(&slow, GAME_EVENT_HARD_DROP, &game);
    fx_make_hard_drop(&game, PIECE_O, FX_SPARK_FULL_SPEED_ROWS);
    fx_on_events(&fast, GAME_EVENT_HARD_DROP, &game);
    for (int i = 0; i < FX_MAX_PARTICLES; ++i) {
        if (slow.particles[i].life > 0.0f) {
            CHECK(slow.particles[i].vy < fast.particles[i].vy);
        }
    }
}

static void test_fx_sparks_expire_and_fall(void)
{
    Fx fx;
    GameState game;
    game_init(&game, 24);
    fx_init(&fx, 25);
    fx_make_hard_drop(&game, PIECE_I, 12);
    fx_on_events(&fx, GAME_EVENT_HARD_DROP, &game);
    CHECK_EQ_INT(fx_count_kind(&fx, FX_PARTICLE_SPARK), 4 * FX_SPARKS_PER_CELL);

    /* Spark gravity is stronger than the glow particles': after a tenth of a
       second every spark is already slower upward than it started. */
    float start_vy[FX_MAX_PARTICLES];
    for (int i = 0; i < FX_MAX_PARTICLES; ++i) {
        start_vy[i] = fx.particles[i].vy;
    }
    for (int step = 0; step < 6; ++step) {
        fx_update(&fx, 1.0f / 60.0f);
    }
    for (int i = 0; i < FX_MAX_PARTICLES; ++i) {
        if (fx.particles[i].life > 0.0f) {
            CHECK(fx.particles[i].vy < start_vy[i] + FX_SPARK_GRAVITY * 0.05f);
        }
    }

    /* None outlives FX_SPARK_LIFE_MAX. */
    const int steps = (int)(FX_SPARK_LIFE_MAX * 60.0f) + 2;
    for (int step = 0; step < steps; ++step) {
        fx_update(&fx, 1.0f / 60.0f);
    }
    CHECK_EQ_INT(fx_particle_count(&fx), 0);
}

static void test_fx_spark_pool_bound(void)
{
    Fx fx;
    GameState game;
    game_init(&game, 26);
    fx_init(&fx, 27);
    fx_make_hard_drop(&game, PIECE_I, 12);

    /* Sparks share the particle pool: it never grows past its capacity, and
       spawns beyond it are dropped and counted. */
    for (int i = 0; i < 200; ++i) {
        fx_on_events(&fx, GAME_EVENT_HARD_DROP, &game);
        CHECK(fx_particle_count(&fx) <= FX_MAX_PARTICLES);
    }
    CHECK_EQ_INT(fx_particle_count(&fx), FX_MAX_PARTICLES);
    CHECK_EQ_INT(fx_count_kind(&fx, FX_PARTICLE_SPARK), FX_MAX_PARTICLES);
    CHECK(fx.particles_dropped > 0);
}

/* --- shockwave rings ----------------------------------------------------- */

/* A clearing lock of `lines` rows from the bottom, scored as `clear`. */
static void fx_make_big_clear(GameState *game, int lines, GameTSpin tspin, bool perfect)
{
    fx_make_line_clear(game, lines);
    memset(&game->last_clear, 0, sizeof game->last_clear);
    game->last_clear.lines = (uint8_t)lines;
    game->last_clear.tspin = (uint8_t)tspin;
    game->last_clear.perfect_clear = perfect;
    game->last_locked = (ActivePiece){ tspin != GAME_TSPIN_NONE ? PIECE_T : PIECE_I, 1, 8, 0 };
}

static void test_fx_clear_rings(void)
{
    Fx fx;
    GameState game;
    game_init(&game, 28);

    /* A plain single, double or triple has no ring. */
    for (int lines = 1; lines <= 3; ++lines) {
        fx_init(&fx, 29);
        fx_make_big_clear(&game, lines, GAME_TSPIN_NONE, false);
        fx_on_events(&fx, GAME_EVENT_LINE_CLEAR, &game);
        CHECK_EQ_INT(fx_ring_count(&fx), 0);
    }

    /* Four lines: one ring in the I's color, centered across the rows. */
    fx_init(&fx, 29);
    fx_make_big_clear(&game, 4, GAME_TSPIN_NONE, false);
    fx_on_events(&fx, GAME_EVENT_LINE_CLEAR, &game);
    CHECK_EQ_INT(fx_ring_count(&fx), 1);
    for (int i = 0; i < FX_MAX_RINGS; ++i) {
        const FxRing *ring = &fx.rings[i];
        if (ring->life <= 0.0f) {
            continue;
        }
        CHECK(fx_near(ring->x, (float)BOARD_WIDTH * 0.5f, 1e-5f));
        CHECK(fx_near(ring->y, 2.0f, 1e-5f)); /* rows 0-3: centers 0.5 .. 3.5 */
        CHECK(fx_near(ring->radius, FX_RING_RADIUS, 1e-5f));
        CHECK_EQ_INT(ring->color, CELL_I);
        CHECK(fx_near(fx_ring_current_radius(ring), 0.0f, 1e-5f));
    }

    /* A T-spin single gets one, a perfect clear two, the second wider. */
    fx_init(&fx, 29);
    fx_make_big_clear(&game, 1, GAME_TSPIN_FULL, false);
    fx_on_events(&fx, GAME_EVENT_LINE_CLEAR, &game);
    CHECK_EQ_INT(fx_ring_count(&fx), 1);
    fx_init(&fx, 29);
    fx_make_big_clear(&game, 2, GAME_TSPIN_NONE, true);
    fx_on_events(&fx, GAME_EVENT_LINE_CLEAR, &game);
    CHECK_EQ_INT(fx_ring_count(&fx), 2);
    CHECK(fx_near(fx.rings[1].radius, FX_RING_PERFECT_RADIUS, 1e-5f));

    /* None with the effects off; fx_reset clears them. */
    fx_init(&fx, 29);
    fx_set_intensity(&fx, 0.0f);
    fx_make_big_clear(&game, 4, GAME_TSPIN_NONE, false);
    fx_on_events(&fx, GAME_EVENT_LINE_CLEAR, &game);
    CHECK_EQ_INT(fx_ring_count(&fx), 0);
    fx_init(&fx, 29);
    fx_on_events(&fx, GAME_EVENT_LINE_CLEAR, &game);
    fx_reset(&fx);
    CHECK_EQ_INT(fx_ring_count(&fx), 0);
}

static void test_fx_rings_grow_and_expire(void)
{
    Fx fx;
    GameState game;
    game_init(&game, 30);
    fx_init(&fx, 31);
    fx_make_big_clear(&game, 4, GAME_TSPIN_NONE, false);
    fx_on_events(&fx, GAME_EVENT_LINE_CLEAR, &game);

    const FxRing *ring = &fx.rings[0];
    float last = fx_ring_current_radius(ring);
    const int steps = (int)(FX_RING_SECONDS * 60.0f);
    for (int step = 0; step < steps; ++step) {
        fx_update(&fx, 1.0f / 60.0f);
        const float radius = fx_ring_current_radius(ring);
        if (ring->life > 0.0f) {
            CHECK(radius > last);
            CHECK(radius <= FX_RING_RADIUS);
        }
        last = radius;
    }
    fx_update(&fx, 2.0f / 60.0f);
    CHECK_EQ_INT(fx_ring_count(&fx), 0);
    CHECK(fx_near(fx_ring_current_radius(ring), FX_RING_RADIUS, 1e-5f));
}

static void test_fx_ring_pool_bound(void)
{
    Fx fx;
    GameState game;
    game_init(&game, 32);
    fx_init(&fx, 33);
    fx_make_big_clear(&game, 4, GAME_TSPIN_NONE, true);

    /* Two rings per perfect clear, many times over: the pool stays full and
       the rings already on screen keep their place. */
    for (int i = 0; i < 20; ++i) {
        fx_on_events(&fx, GAME_EVENT_LINE_CLEAR, &game);
        CHECK(fx_ring_count(&fx) <= FX_MAX_RINGS);
    }
    CHECK_EQ_INT(fx_ring_count(&fx), FX_MAX_RINGS);
    for (int i = 0; i < FX_MAX_RINGS; ++i) {
        CHECK(fx.rings[i].life == FX_RING_SECONDS);
    }

    /* Once they expire, new ones fit again. */
    for (int step = 0; step < (int)(FX_RING_SECONDS * 60.0f) + 2; ++step) {
        fx_update(&fx, 1.0f / 60.0f);
    }
    CHECK_EQ_INT(fx_ring_count(&fx), 0);
    fx_on_events(&fx, GAME_EVENT_LINE_CLEAR, &game);
    CHECK_EQ_INT(fx_ring_count(&fx), 2);
}

static void test_fx_clear_visual(void)
{
    FxClearVisual visual;

    fx_clear_visual(0.0f, &visual);
    CHECK(visual.progress == 0.0f);
    CHECK(visual.flash == 0.0f);
    for (int x = 0; x < BOARD_WIDTH; ++x) {
        CHECK(visual.visible[x] == 1.0f);
    }

    fx_clear_visual(1.0f, &visual);
    CHECK(visual.progress == 1.0f);
    CHECK(visual.flash == 0.0f);
    for (int x = 0; x < BOARD_WIDTH; ++x) {
        CHECK(visual.visible[x] == 0.0f);
    }

    /* Out-of-range progress is clamped, not extrapolated. */
    fx_clear_visual(-2.0f, &visual);
    CHECK(visual.progress == 0.0f);
    CHECK(visual.flash == 0.0f);
    fx_clear_visual(2.0f, &visual);
    CHECK(visual.progress == 1.0f);
    CHECK(visual.visible[0] == 0.0f);

    /* The flash peaks exactly at the documented keyframe. */
    fx_clear_visual(FX_CLEAR_FLASH_PEAK, &visual);
    CHECK(visual.flash == 1.0f);
    fx_clear_visual(FX_CLEAR_FLASH_END, &visual);
    CHECK(visual.flash == 0.0f);

    /* Every column only ever shrinks, and stays in 0..1. The flash rises to the
       peak and only fades after it. */
    float previous[BOARD_WIDTH];
    for (int x = 0; x < BOARD_WIDTH; ++x) {
        previous[x] = 2.0f;
    }
    float last_flash = -1.0f;
    for (int step = 0; step <= 100; ++step) {
        const float t = (float)step / 100.0f;
        fx_clear_visual(t, &visual);
        CHECK(visual.flash >= 0.0f && visual.flash <= 1.0f);
        if (t <= FX_CLEAR_FLASH_PEAK) {
            CHECK(visual.flash >= last_flash - 1e-6f);
        } else {
            CHECK(visual.flash <= last_flash + 1e-6f);
        }
        last_flash = visual.flash;
        for (int x = 0; x < BOARD_WIDTH; ++x) {
            CHECK(visual.visible[x] >= 0.0f && visual.visible[x] <= 1.0f);
            CHECK(visual.visible[x] <= previous[x] + 1e-6f);
            previous[x] = visual.visible[x];
        }
    }

    /* The sweep really does run from the centre columns outwards. */
    fx_clear_visual(0.60f, &visual);
    CHECK(visual.visible[BOARD_WIDTH / 2] == 0.0f);
    CHECK(visual.visible[BOARD_WIDTH / 2 - 1] == 0.0f);
    CHECK(visual.visible[0] == 1.0f);
    CHECK(visual.visible[BOARD_WIDTH - 1] == 1.0f);
}

void test_fx_suite(void)
{
    test_fx_init_and_reset();
    test_fx_line_clear_particles();
    test_fx_level_pulse();
    test_fx_particles_expire();
    test_fx_update_clamps_dt();
    test_fx_pool_capacity_and_dropping();
    test_fx_hard_drop_sparks();
    test_fx_sparks_expire_and_fall();
    test_fx_spark_pool_bound();
    test_fx_clear_rings();
    test_fx_rings_grow_and_expire();
    test_fx_ring_pool_bound();
    test_fx_deterministic();
    test_fx_lock_flash();
    test_fx_hard_drop_trail_and_shake();
    test_fx_hard_drop_edges();
    test_fx_clear_visual();
}
