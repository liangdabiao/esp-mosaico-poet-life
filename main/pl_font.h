// SPDX-License-Identifier: Apache-2.0
// 24x24 单色位图字模：中英文混排共用一张按码点升序的表。
#pragma once

#include <stddef.h>

#include "mosaico_raylib_fast.h"
#include "pl_cjk_glyph_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 解码一个 UTF-8 码点，返回它占的字节数（非法字节按 1 处理）。 */
size_t pl_font_utf8_next(const char *s, uint32_t *cp);

/* 二分查找字模；字库没有该码点时返回 NULL。 */
const pl_font_glyph_t *pl_font_find(uint32_t cp);

/* 码点的步进宽度（像素），字库没有则为 0。 */
int pl_font_advance(uint32_t cp);

/* 以 (x,y) 为左上角绘制单个码点，返回下一像素 x。 */
int pl_font_draw_cp(uint32_t cp, int x, int y, Color color);

/* 绘制整串（中英混排），返回下一像素 x；缺字按步进跳过，不会错位。 */
int pl_font_draw(const char *utf8, int x, int y, Color color);

/* 字符串像素宽度。 */
int pl_font_width(const char *utf8);

/* [byte_start, byte_end) 这一段的像素宽度。 */
int pl_font_width_range(const char *utf8, size_t byte_start, size_t byte_end);

#define PL_FONT_LINE_HEIGHT PL_FONT_CELL

#ifdef __cplusplus
}
#endif
