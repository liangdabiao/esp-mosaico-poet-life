// SPDX-License-Identifier: Apache-2.0
// CJK 换行排版：按像素宽度断行、行首/行尾禁则、ASCII 数字串不拆。
#pragma once

#include "pl_font.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 一次排版最多记录的可见行数。40 行是奔着最长的文本（结局履历 15 条）来的：
 * 排版结果只缓存在 game 状态里（约 256 字节，按工程规则不上调用栈），
 * 分页只是取 lines[first..first+n)，不会像小上限那样第 2 页起就空掉。 */
#define PL_TEXT_MAX_LINES 40

typedef struct {
    uint16_t begin;   /* 行首字节偏移 */
    uint16_t end;     /* 行尾字节偏移（不含） */
    uint16_t width;   /* 该行像素宽度 */
} pl_text_line_t;

typedef struct {
    pl_text_line_t lines[PL_TEXT_MAX_LINES];
    int count;        /* lines[] 中已填的行数 */
    int total;        /* 全文需要的行数 */
} pl_text_layout_t;

/* 把整串按 max_width 排版；out 由调用方持有（约 256 字节，放状态里不放栈）。 */
void pl_text_layout(const char *utf8, int max_width, pl_text_layout_t *out);

/* 绘制 [first_line, first_line + max_lines) 行，返回实际绘制的行数。
 * 行序越界时不画，靠 total/count 做分页，不会画错别的内容。 */
int pl_text_draw_lines(const char *utf8, const pl_text_layout_t *lay, int first_line,
                       int max_lines, int x, int y, int line_pitch, Color color);

/* 居中绘制一行（已知 begin/end）。 */
int pl_text_draw_centered(const char *utf8, size_t begin, size_t end, int center_x,
                          int y, Color color);

#ifdef __cplusplus
}
#endif
