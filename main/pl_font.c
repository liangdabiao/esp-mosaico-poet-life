// SPDX-License-Identifier: Apache-2.0
// 字模查找与 blit。字模表由 tools/gen_cjk_font.py 生成到 pl_cjk_glyph_data.h。
#include "pl_font.h"

#include <stddef.h>

#include "pl_cjk_glyph_data.h"

size_t pl_font_utf8_next(const char *s, uint32_t *cp)
{
    const unsigned char *u = (const unsigned char *)s;
    if (u[0] < 0x80) {
        *cp = u[0];
        return 1;
    }
    if ((u[0] & 0xE0) == 0xC0) {
        *cp = ((uint32_t)(u[0] & 0x1F) << 6) | (uint32_t)(u[1] & 0x3F);
        return 2;
    }
    if ((u[0] & 0xF0) == 0xE0) {
        *cp = ((uint32_t)(u[0] & 0x0F) << 12) | ((uint32_t)(u[1] & 0x3F) << 6) |
              (uint32_t)(u[2] & 0x3F);
        return 3;
    }
    *cp = u[0];
    return 1;
}

const pl_font_glyph_t *pl_font_find(uint32_t cp)
{
    int lo = 0, hi = PL_FONT_GLYPH_COUNT - 1;
    while (lo <= hi) {
        const int mid = lo + (hi - lo) / 2;
        const uint32_t probe = PL_FONT_GLYPHS[mid].cp;
        if (probe == cp) return &PL_FONT_GLYPHS[mid];
        if (probe < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return NULL;
}

int pl_font_advance(uint32_t cp)
{
    const pl_font_glyph_t *g = pl_font_find(cp);
    return g ? g->advance : 0;
}

int pl_font_draw_cp(uint32_t cp, int x, int y, Color color)
{
    const pl_font_glyph_t *g = pl_font_find(cp);
    if (!g) return x;
    for (int r = 0; r < PL_FONT_CELL; r++) {
        uint32_t row = g->rows[r];
        if (!row) continue;
        int c = 0;
        while (c < PL_FONT_CELL) {
            if (!(row & (1u << (PL_FONT_CELL - 1 - c)))) {
                ++c;
                continue;
            }
            int run = 1;                        /* 水平游程合成一次填充，逐像素画太慢 */
            while (c + run < PL_FONT_CELL &&
                   (row & (1u << (PL_FONT_CELL - 1 - (c + run))))) {
                ++run;
            }
            DrawRectangle(x + c, y + r, run, 1, color);
            c += run;
        }
    }
    return x + g->advance;
}

int pl_font_draw(const char *utf8, int x, int y, Color color)
{
    if (!utf8) return x;
    int pen = x;
    const char *s = utf8;
    while (*s) {
        uint32_t cp = 0;
        s += pl_font_utf8_next(s, &cp);
        pen = pl_font_draw_cp(cp, pen, y, color);
    }
    return pen;
}

int pl_font_width(const char *utf8)
{
    return pl_font_width_range(utf8, 0, utf8 ? (size_t)-1 : 0);
}

int pl_font_width_range(const char *utf8, size_t byte_start, size_t byte_end)
{
    if (!utf8) return 0;
    int w = 0;
    size_t i = byte_start;
    while (utf8[i] && i < byte_end) {
        uint32_t cp = 0;
        i += pl_font_utf8_next(&utf8[i], &cp);
        w += pl_font_advance(cp);
    }
    return w;
}
