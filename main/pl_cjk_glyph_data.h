// SPDX-License-Identifier: Apache-2.0
// 由 tools/gen_cjk_font.py 自动生成 —— 勿手改。
// 母字体：Noto Sans CJK SC Regular（SIL OFL 1.1）；2288 个字模，24x24 单色，按码点升序（pl_font.c 二分查找）。
// 表体在 pl_cjk_glyph_data.c：头文件里的 static 数组会让每个编译单元
// 各自复制一份 600+KB 字模，还会让跨文件的指针比较必然不相等。
#pragma once

#include <stdint.h>

#define PL_FONT_CELL 24
#define PL_FONT_SIZE 24

typedef struct {
    uint32_t cp;
    uint32_t rows[PL_FONT_CELL];   /* bit (PL_FONT_CELL-1-x) = 第 x 列 */
    uint8_t advance;               /* 中文一格，ASCII 按墨迹宽度 */
} pl_font_glyph_t;

#define PL_FONT_GLYPH_COUNT 2288
extern const pl_font_glyph_t PL_FONT_GLYPHS[PL_FONT_GLYPH_COUNT];

