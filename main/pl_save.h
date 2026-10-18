// SPDX-License-Identifier: Apache-2.0
// 生涯存档的纯逻辑层：定长小端字节布局 + magic/version/checksum。
// 设备端 pl_store.c 与宿主 pl_store_host.c 只搬运这段字节，格式校验在这里。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "game.h"
#include "poets_game.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PL_SAVE_BYTES 40 /* encode() 写出的字节数，见 pl_save.c 的字段表 */

typedef struct {
    int16_t poet_index;   /* g_poets 下标；-1 表示没有存档 */
    int16_t page;         /* pl_page_t，恢复时直接回到那一页 */
    uint8_t turn;
    uint8_t last_category;
    uint8_t dead;
    uint8_t death_reason;
    uint8_t has_rank;
    uint16_t poems;
    int16_t silver, food, fame, health, muse, friends;
    uint32_t rng;
} pl_save_data_t;

/* 从一场进行中的生涯抓取可持久化字段（poet 指针换成表下标）。 */
void pl_save_capture(pl_save_data_t *out, const poets_game_t *core, int poet_index,
                     int page);

/* 写回 core。调用方负责按 poet_index 取回 poets_poet_t 并赋给 core->poet。 */
void pl_save_restore(const pl_save_data_t *in, poets_game_t *core);

size_t pl_save_encode(const pl_save_data_t *in, uint8_t *bytes, size_t capacity);
/* 校验失败（长度、magic、版本、checksum 任一不符）返回 false 且不写 out。 */
bool pl_save_decode(const uint8_t *bytes, size_t size, pl_save_data_t *out);

#ifdef __cplusplus
}
#endif
