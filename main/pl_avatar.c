// SPDX-License-Identifier: Apache-2.0
// 头像绘制：一行一个同色游程，一次 DrawRectangle；逐像素画会拖垮 30 帧。
#include "pl_avatar.h"

void pl_avatar_draw(int poet_index, int x, int y, int dx)
{
    if (poet_index < 0 || poet_index >= PL_AVATAR_COUNT) return;
    const pl_avatar_rgb_t *pal = pl_avatar_palette[poet_index];
    Color cols[PL_AVATAR_COLORS];
    for (int i = 0; i < PL_AVATAR_COLORS; i++) {
        cols[i] = (Color){pal[i].r, pal[i].g, pal[i].b, 255};
    }
    const pl_avatar_span_t *spans = pl_avatar_spans[poet_index];
    const uint16_t count = pl_avatar_span_count[poet_index];
    for (uint16_t i = 0; i < count; i++) {
        const pl_avatar_span_t *s = &spans[i];
        DrawRectangle(x + s->x + dx, y + s->y, s->w, 1, cols[s->color]);
    }
}
