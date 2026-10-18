// SPDX-License-Identifier: Apache-2.0
#include "pl_text.h"

#include <string.h>

/* 禁则按码点比较：多字节字符写成 '×' 会被编译成实现相关的多字符常量，
 * 与解码出来的码点根本不相等。 */
static int is_no_start(uint32_t cp)
{
    switch (cp) {
    case ',': case '.': case ';': case ':': case '?': case '!':
    case ')': case ']': case '}': case '%':
    case 0xFF0C: case 0x3002: case 0x3001: case 0xFF1B: case 0xFF1A:
    case 0xFF1F: case 0xFF01: case 0xFF09: case 0xFF3D: case 0xFF5D:
    case 0x300B: case 0x300D: case 0x300F: case 0xFF05: case 0x30FB:
    case 0x2026: case 0x201D: case 0x2019:
        return 1;
    default:
        return 0;
    }
}

static int is_no_end(uint32_t cp)
{
    switch (cp) {
    case '(': case '[': case '{':
    case 0xFF08: case 0xFF3B: case 0xFF5B:
    case 0x300A: case 0x300C: case 0x300E:
    case 0x201C: case 0x2018:
        return 1;
    default:
        return 0;
    }
}

/* 数字与英文串是一个整体，不能从中间断开（"3/15"、"80" 都要成块换行）。
 * 分隔符只在「后面还接着字母数字」时才并入本块，所以 "Done." 的句号、
 * "3/十五" 的斜杠都留在块外，交给行首禁则处理。 */
static int is_alpha_num(uint32_t cp)
{
    return (cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'z') ||
           (cp >= 'A' && cp <= 'Z');
}

static int is_join(uint32_t cp)
{
    return cp == '/' || cp == '.' || cp == '-' || cp == '+' || cp == '%' ||
           cp == '_' || cp == 0x00D7;
}

/* 从 i 起取一个不可再分的排版块，返回块宽并把 *next 指向块后。 */
static int next_atom(const char *s, size_t i, size_t *next, uint32_t *first_cp,
                     size_t *first_n)
{
    uint32_t cp = 0;
    const size_t n = pl_font_utf8_next(&s[i], &cp);
    *first_cp = cp;
    *first_n = n;
    int w = pl_font_advance(cp);
    size_t j = i + n;
    if (!is_alpha_num(cp)) {
        *next = j;
        return w;
    }
    while (s[j]) {
        uint32_t c2 = 0;
        const size_t n2 = pl_font_utf8_next(&s[j], &c2);
        int take = is_alpha_num(c2);
        if (!take && is_join(c2) && s[j + n2]) {
            uint32_t c3 = 0;
            pl_font_utf8_next(&s[j + n2], &c3);
            take = is_alpha_num(c3);
        }
        if (!take) break;
        w += pl_font_advance(c2);
        j += n2;
    }
    *next = j;
    return w;
}

static int draw_range(const char *s, size_t begin, size_t end, int x, int y, Color color)
{
    int pen = x;
    size_t i = begin;
    while (s[i] && i < end) {
        uint32_t cp = 0;
        i += pl_font_utf8_next(&s[i], &cp);
        pen = pl_font_draw_cp(cp, pen, y, color);
    }
    return pen - x;
}

static void emit_line(pl_text_layout_t *out, const char *s, size_t begin, size_t end)
{
    if (end <= begin) return;
    const int w = pl_font_width_range(s, begin, end);
    if (out->total < PL_TEXT_MAX_LINES) {
        out->lines[out->count].begin = (uint16_t)begin;
        out->lines[out->count].end = (uint16_t)end;
        out->lines[out->count].width = (uint16_t)w;
        out->count++;
    }
    out->total++;
}

void pl_text_layout(const char *s, int max_width, pl_text_layout_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!s || !*s || max_width <= 0) return;

    size_t i = 0, line_begin = 0, prev_begin = 0;
    int line_w = 0, prev_w = 0;
    uint32_t prev_cp = 0;
    int prev_single = 0;
    while (s[i]) {
        const size_t atom_begin = i;
        uint32_t first_cp = 0;
        size_t first_n = 0, next = 0;
        const int atom_w = next_atom(s, i, &next, &first_cp, &first_n);
        i = next;

        if (line_w + atom_w > max_width && atom_begin > line_begin) {
            /* 这一整块放不进当前行：默认在它之前断行；若它是个不能置行首的
             * 标点，就把前一块一起挤到下一行，标点跟着它前面的字走。同理，
             * 前一块若是不能收尾的开启符号（（ 《 「 等），也要跟着下一行走。 */
            size_t cut = atom_begin;
            int carry = 0;
            const int pull = is_no_start(first_cp) ||
                             (prev_single && is_no_end(prev_cp));
            if (pull && prev_begin > line_begin) {
                cut = prev_begin;
                carry = prev_w;
            }
            emit_line(out, s, line_begin, cut);
            line_begin = cut;
            line_w = carry;
        }
        line_w += atom_w;
        prev_begin = atom_begin;
        prev_w = atom_w;
        prev_cp = first_cp;
        prev_single = (i - atom_begin) == first_n;
    }
    emit_line(out, s, line_begin, i);
}

int pl_text_draw_lines(const char *utf8, const pl_text_layout_t *lay, int first_line,
                       int max_lines, int x, int y, int line_pitch, Color color)
{
    if (!utf8 || !lay || first_line < 0) return 0;
    int drawn = 0;
    for (int k = first_line; k < first_line + max_lines && k < lay->count; k++) {
        draw_range(utf8, lay->lines[k].begin, lay->lines[k].end, x,
                   y + (k - first_line) * line_pitch, color);
        drawn++;
    }
    return drawn;
}

int pl_text_draw_centered(const char *utf8, size_t begin, size_t end, int center_x,
                          int y, Color color)
{
    const int w = pl_font_width_range(utf8, begin, end);
    return draw_range(utf8, begin, end, center_x - w / 2, y, color);
}
