// SPDX-License-Identifier: Apache-2.0
// 由 tools/gen_poet_avatars.py 生成，请勿手改。
#pragma once

#include <stdint.h>

#define PL_AVATAR_W 40
#define PL_AVATAR_H 40
#define PL_AVATAR_COLORS 5
#define PL_AVATAR_COUNT 6

typedef struct {
    uint8_t x;      /* 相对头像左边缘的像素列 */
    uint8_t y;
    uint8_t w;      /* 同色水平游程长度 */
    uint8_t color;  /* pl_avatar_palette[诗人][color] */
} pl_avatar_span_t;

typedef struct { uint8_t r, g, b; } pl_avatar_rgb_t;

extern const pl_avatar_rgb_t pl_avatar_palette[PL_AVATAR_COUNT][PL_AVATAR_COLORS];
extern const pl_avatar_span_t *const pl_avatar_spans[PL_AVATAR_COUNT];
extern const uint16_t pl_avatar_span_count[PL_AVATAR_COUNT];
