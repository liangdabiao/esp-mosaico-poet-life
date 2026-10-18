// SPDX-License-Identifier: Apache-2.0
// 《诗与远方》规则核心：诗人 15 回合生存模拟。纯 C11、零平台依赖，
// 设备固件与宿主测试共用（移植自原版 index.html，数值公式一一对应）。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "generated/poets_data.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const poets_poet_t *poet;
    uint8_t turn;          // 1..POETS_MAX_TURNS while playing
    int16_t silver, food, fame, health, muse, friends;
    uint16_t poems;
    bool has_rank;
    uint8_t last_category; // poets_action_cat_t of the previous action
    bool dead;
    uint8_t death_reason;  // index into poets_death_reason_name()
    uint32_t rng;          // xorshift state
} poets_game_t;

typedef struct {
    int32_t score;
    uint8_t title;         // index into poets_ending_title()
    // final stats snapshot for the ending screen
    int16_t silver, food, fame, health, friends;
    uint16_t poems;
} poets_ending_t;

/* Lifecycle -------------------------------------------------------------- */

// Standalone xorshift for caller-side policy randomness (host tests, ambient
// shuffling). Independent of the game's own RNG stream.
uint32_t poets_game_rand(uint32_t *state);

void poets_game_init(poets_game_t *g, const poets_poet_t *poet, uint32_t seed);

/* Actions -----------------------------------------------------------------*/

// Total action count for this run: the 12 common actions plus the poet's
// special action when the poet has one (special is always last).
int poets_game_action_count(const poets_game_t *g);
const poets_action_def_t *poets_game_action_def(const poets_game_t *g, int index);
bool poets_game_action_enabled(const poets_game_t *g, int index);
// Category this action quizzes on, or POETS_CAT_NONE (静养休息).
int poets_game_action_quiz(const poets_game_t *g, int index);

/* Resolves the action: applies computed effects (multipliers and randomness
 * exactly as the original) and writes the narrative log line into `log`. */
void poets_game_resolve_action(poets_game_t *g, int index, bool correct,
                               char *log, size_t log_size);

/* Question / events -------------------------------------------------------*/

const poets_question_t *poets_game_pick_question(poets_game_t *g, int category);

// 25% chance of the poet's special event, otherwise a common one.
const poets_event_t *poets_game_pick_event(poets_game_t *g);

// Applies one choice (flat or quiz branch) and writes the result text.
void poets_game_apply_choice(poets_game_t *g, const poets_choice_t *choice,
                             bool quiz_correct, char *result, size_t result_size);

/* Turn / month ------------------------------------------------------------*/

// Salary, food consumption, starvation penalty. Writes the starvation line
// (or empty string) into `log` when something notable happened.
void poets_game_process_month(poets_game_t *g, char *log, size_t log_size);

// Advances the turn counter. Returns false when the run is over
// (max turns reached or already dead) — the caller shows the ending.
bool poets_game_advance_turn(poets_game_t *g);

/* Ending ------------------------------------------------------------------*/

void poets_game_ending(const poets_game_t *g, poets_ending_t *out);

/* Table accessors (for UI and AI persona) ----------------------------------*/

const char *poets_death_reason_name(uint8_t index);
const char *poets_death_reason_text(uint8_t index);
const char *poets_ending_title(uint8_t index);
const char *poets_ending_subtitle(uint8_t index);
const char *poets_ending_seal(uint8_t index);
int poets_ending_title_count(void);
// "第 N 回合 · 大唐·天宝三年 · 正月" style label for turn (1-based).
const char *poets_game_era_label(uint8_t turn_zero_based);

#ifdef __cplusplus
}
#endif
