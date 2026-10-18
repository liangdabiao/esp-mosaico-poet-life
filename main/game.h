// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct game_t *game_handle_t;
typedef struct {
    uint16_t width;
    uint16_t height;
} game_config_t;

/* 页面 id 同时进入快照与 state_json，headless replay 才能断言「点了哪一格、
 * 走到了哪一页」——只看 tick 是证明不了交互的。 */
enum {
    PL_PAGE_TITLE = 0,   /* 选诗人 */
    PL_PAGE_BIO,         /* 诗人小传 + 初始六维 */
    PL_PAGE_HOME,        /* 月度面板：状态 + 叙事 */
    PL_PAGE_ACTION,      /* 行动页签 */
    PL_PAGE_QUIZ,        /* 答题 */
    PL_PAGE_EVENT,       /* 事件抉择 */
    PL_PAGE_ENDING       /* 死因 / 结局 + 生涯履历 */
};

enum { PL_PAGE_COUNT = 7 };

typedef struct {
    uint32_t tick;
    int32_t pointer_x;
    int32_t pointer_y;
    bool pointer_down;
    bool paused;
    uint8_t page;
    uint8_t tab;             /* 行动页签：0 生计 / 1 交游 / 2 修身 */
    uint16_t turn;           /* 1..15，0 表示还没开局 */
    int16_t silver, food, fame, health, muse, friends;
    uint16_t poems;
    bool dead;
    bool event_pending;      /* 事件还没选 */
} game_snapshot_t;

/* Portable C shared by Host and device. One loop owns each instance and calls
 * these methods; input adapters enqueue or deliver events on that same loop. */
bool game_create(const game_config_t *config, game_handle_t *ret_handle);
void game_delete(game_handle_t handle);
void game_reset(game_handle_t handle);
void game_set_paused(game_handle_t handle, bool paused);
void game_set_pointer(game_handle_t handle, int32_t x, int32_t y, bool pressed);
void game_update(game_handle_t handle);
bool game_render(game_handle_t handle);
game_snapshot_t game_read(game_handle_t handle);
