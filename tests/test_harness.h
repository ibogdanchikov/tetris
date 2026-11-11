#pragma once

/*
 * Minimal assert-style test harness: no external framework, no SDL, no GL.
 * Every failed CHECK is printed with its file and line; test_main returns the
 * number of failures so ctest reports the run as failed.
 *
 * The suites live in their own files and share the failure counter declared
 * here, so a failure anywhere makes the single test executable fail.
 */

#include <stdio.h>

extern int test_failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            ++test_failures;                                                     \
        }                                                                        \
    } while (0)

#define CHECK_EQ_INT(actual, expected)                                           \
    do {                                                                         \
        const long check_actual = (long)(actual);                                \
        const long check_expected = (long)(expected);                            \
        if (check_actual != check_expected) {                                    \
            fprintf(stderr, "FAIL %s:%d: %s == %s (got %ld, expected %ld)\n",    \
                    __FILE__, __LINE__, #actual, #expected, check_actual,        \
                    check_expected);                                             \
            ++test_failures;                                                     \
        }                                                                        \
    } while (0)

/* Suites defined in tests/test_piece.c, tests/test_game.c, tests/test_rules.c,
   tests/test_input_buffer.c, tests/test_font.c, tests/test_fx.c and
   tests/test_audio.c. */
void test_piece_suite(void);
void test_game_suite(void);
void test_rules_suite(void);
void test_input_suite(void);
void test_font_suite(void);
void test_fx_suite(void);
void test_audio_suite(void);
