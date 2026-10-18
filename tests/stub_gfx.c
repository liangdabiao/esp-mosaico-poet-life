// SPDX-License-Identifier: Apache-2.0
// 记录 blit 调用，供宿主测试断言（不产生任何图形输出）。
#include "mosaico_raylib_fast.h"

int g_rect_count;
int g_rect_min_x, g_rect_min_y, g_rect_max_x, g_rect_max_y;
int g_rect_any;

void DrawRectangle(int x, int y, int width, int height, Color color)
{
    (void)color;
    g_rect_count++;
    if (!g_rect_any) {
        g_rect_min_x = x; g_rect_min_y = y;
        g_rect_max_x = x + width; g_rect_max_y = y + height;
        g_rect_any = 1;
        return;
    }
    if (x < g_rect_min_x) g_rect_min_x = x;
    if (y < g_rect_min_y) g_rect_min_y = y;
    if (x + width > g_rect_max_x) g_rect_max_x = x + width;
    if (y + height > g_rect_max_y) g_rect_max_y = y + height;
}

int GetScreenWidth(void) { return 480; }
int GetScreenHeight(void) { return 480; }
