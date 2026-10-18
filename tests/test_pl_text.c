// SPDX-License-Identifier: Apache-2.0
// 宿主单元测试：字模查找（含二分的前提假设）+ CJK 换行禁则 + 分页绘制边界。
// 编译见 tests/build_tests.ps1（用 tests/host_compat 的最小引擎替身）。
#include <stdio.h>
#include <string.h>

#include "pl_text.h"
#include "pl_cjk_glyph_data.h"

extern int g_rect_count;
extern int g_rect_any;

static int failures;
static int checks;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        checks++;                                                            \
        if (!(cond)) {                                                       \
            printf("FAIL: %s\n", msg);                                       \
            failures++;                                                      \
        }                                                                    \
    } while (0)

#define RESET_GFX() (g_rect_count = 0, g_rect_any = 0)

static int starts_forbidden(const pl_text_layout_t *lay, const char *s)
{
    static const uint32_t bad[] = { 0xFF0C, 0x3002, 0x3001, 0xFF1B, 0xFF1A,
                                    0xFF1F, 0xFF01, 0xFF09, 0x300B, 0x2026 };
    for (int k = 1; k < lay->count; k++) {          /* 第 0 行行首由文本本身决定 */
        uint32_t cp = 0;
        pl_font_utf8_next(s + lay->lines[k].begin, &cp);
        for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
            if (cp == bad[i]) return 1;
        }
    }
    return 0;
}

static int starts_with(const char *s, const pl_text_layout_t *lay, char bad)
{
    for (int k = 0; k < lay->count; k++) {
        if (s[lay->lines[k].begin] == bad) return 1;
    }
    return 0;
}

static int ends_with(const char *s, const pl_text_layout_t *lay, char bad)
{
    for (int k = 0; k < lay->count; k++) {
        if (s[lay->lines[k].end - 1] == bad) return 1;
    }
    return 0;
}

static void test_font_lookup(void)
{
    CHECK(pl_font_find(0x674E) != NULL, "李 baked");      /* 李白 */
    CHECK(pl_font_find(0x767D) != NULL, "白 baked");
    CHECK(pl_font_find(0x8BD7) != NULL, "诗 baked");
    CHECK(pl_font_find('0') != NULL, "digit baked");
    CHECK(pl_font_find(' ') != NULL, "space baked");
    CHECK(pl_font_advance(0x674E) == PL_FONT_CELL, "CJK advance is a full cell");
    CHECK(pl_font_advance('1') > 0 && pl_font_advance('1') < PL_FONT_CELL,
          "ASCII advance is proportional");
    CHECK(pl_font_find(0x9FFF) == NULL, "unbaked code point reports missing");
    CHECK(pl_font_advance(0x9FFF) == 0, "missing glyph has no advance");

    /* 二分查找依赖「表按码点严格升序」，这里连表长一起验，防止生成器改动破坏前提 */
    for (int i = 0; i < PL_FONT_GLYPH_COUNT; i++) {
        CHECK(pl_font_find(PL_FONT_GLYPHS[i].cp) == &PL_FONT_GLYPHS[i],
              "binary search hits every baked glyph");
        if (i > 0) {
            CHECK(PL_FONT_GLYPHS[i - 1].cp < PL_FONT_GLYPHS[i].cp,
                  "glyph table strictly ascending");
        }
    }
}

static void test_blit(void)
{
    const Color white = { 255, 255, 255, 255 };
    RESET_GFX();
    const int pen = pl_font_draw("李", 10, 20, white);
    CHECK(g_rect_count > 0, "drawing a baked glyph emits runs");
    CHECK(pen == 10 + PL_FONT_CELL, "pen advances one cell per CJK char");

    RESET_GFX();
    CHECK(pl_font_draw("鿿", 0, 0, white) == 0, "missing glyph leaves pen untouched");
    CHECK(g_rect_count == 0, "missing glyph emits nothing");

    /* 游程合成：一整行全黑只该产生一次填充，逐像素画会慢一个数量级 */
    RESET_GFX();
    pl_font_draw("一", 0, 0, white);
    CHECK(g_rect_count >= 1 && g_rect_count <= PL_FONT_CELL,
          "horizontal runs merge into one rectangle per row");
}

static void test_wrap_rules(void)
{
    pl_text_layout_t lay;
    static char big[4096];

    pl_text_layout("", 240, &lay);
    CHECK(lay.total == 0 && lay.count == 0, "empty text needs no line");
    pl_text_layout("银两", 0, &lay);
    CHECK(lay.total == 0, "non-positive width lays out nothing");

    /* 40 个整宽字，240px 每行 10 字 => 4 行 */
    const char *forty = "种豆南山下草盛豆苗稀种豆南山下草盛豆苗稀"
                        "种豆南山下草盛豆苗稀种豆南山下草盛豆苗稀";
    CHECK(strlen(forty) == 40 * 3, "the forty sample really is 40 full-width chars");
    pl_text_layout(forty, 240, &lay);
    CHECK(lay.total == 4, "40 full-width chars wrap into 4 lines at 10 per line");
    for (int k = 0; k < lay.count; k++) {
        CHECK(lay.lines[k].width <= 240, "every line fits the box");
    }

    /* 行尾禁则：句号不能出现在行首 */
    static char punct[4096];
    strcpy(punct, "你写得一手好墓志铭家属赞不绝口润笔八十两，");
    for (int k = 0; k < 6; k++) strcat(punct, "但是诗兴枯竭了名声也受损了，");
    pl_text_layout(punct, 24 * 12, &lay);
    CHECK(lay.total > 1, "punctuated text actually wraps");
    CHECK(starts_forbidden(&lay, punct) == 0, "no line starts with closing punctuation");

    /* ASCII 数字块不许从中间断开：润笔 "187" 与回合 "3/15" 都要成块换行 */
    const char *nums = "一二三四五六七八九十润笔187两当前回合3/15名声大振";
    pl_text_layout(nums, 24 * 9, &lay);
    const size_t off187 = (size_t)(strstr(nums, "187") - nums);
    const size_t off315 = (size_t)(strstr(nums, "3/15") - nums);
    int whole187 = 0, whole315 = 0;
    for (int k = 0; k < lay.count; k++) {
        if (lay.lines[k].begin <= off187 && off187 + 3 <= lay.lines[k].end) whole187 = 1;
        if (lay.lines[k].begin <= off315 && off315 + 4 <= lay.lines[k].end) whole315 = 1;
        CHECK(lay.lines[k].width <= 24 * 9, "numbered text respects the box");
    }
    CHECK(whole187, "the 187 atom lands intact on one line");
    CHECK(whole315, "the 3/15 atom lands intact on one line");
    CHECK(starts_with(nums, &lay, '/') == 0, "no line starts with a slash");
    CHECK(ends_with(nums, &lay, '/') == 0, "no line ends with a dangling slash");

    /* 超过上限：只填 count 行，但 total 继续累加，分页才有依据 */
    big[0] = '\0';
    for (int k = 0; k < 42; k++) strcat(big, "干谒求荐写墓志铭卖字画诗");   /* 12 字 */
    pl_text_layout(big, 24 * 10, &lay);
    CHECK(lay.count == PL_TEXT_MAX_LINES, "layout storage saturates at MAX_LINES");
    CHECK(lay.total == 51, "total keeps counting past the saturation");
}

static void test_measure_matches_layout(void)
{
    pl_text_layout_t lay;
    const char *s = "你是诗圣杜甫。生逢安史之乱，半生颠沛。小儿子饿死，你自己也常常断粮。";
    pl_text_layout(s, 24 * 11, &lay);
    int sum = 0;
    for (int k = 0; k < lay.count; k++) {
        sum += lay.lines[k].width;
        CHECK(lay.lines[k].end > lay.lines[k].begin, "line has bytes");
        if (k > 0) {
            CHECK(lay.lines[k].begin == lay.lines[k - 1].end, "lines are contiguous");
        }
    }
    CHECK(lay.lines[lay.count - 1].end == strlen(s), "layout consumes the whole string");
    CHECK(sum == pl_font_width(s), "summed line widths equal the flat measurement");
}

static void test_page_draw(void)
{
    pl_text_layout_t lay;
    const Color white = { 255, 255, 255, 255 };
    static char big[4096];
    big[0] = '\0';
    for (int k = 0; k < 20; k++) strcat(big, "种豆南山下草盛豆苗稀");   /* 10 字 */
    pl_text_layout(big, 24 * 10, &lay);
    CHECK(lay.total == 20, "20 lines expected");

    CHECK(pl_text_draw_lines(big, &lay, 0, 6, 0, 0, 26, white) == 6, "first page draws 6");
    CHECK(pl_text_draw_lines(big, &lay, 6, 6, 0, 0, 26, white) == 6, "second page draws 6");
    CHECK(pl_text_draw_lines(big, &lay, 18, 6, 0, 0, 26, white) == 2, "last page draws 2");
    CHECK(pl_text_draw_lines(big, &lay, 25, 6, 0, 0, 26, white) == 0, "past the end draws nothing");
    CHECK(pl_text_draw_lines(big, &lay, -1, 6, 0, 0, 26, white) == 0, "negative page is safe");
}

int main(void)
{
    test_font_lookup();
    test_blit();
    test_wrap_rules();
    test_measure_matches_layout();
    test_page_draw();
    if (failures == 0) {
        printf("pl_text_test: ALL PASSED (%d assertions)\n", checks);
        return 0;
    }
    printf("pl_text_test: %d failure(s)\n", failures);
    return 1;
}
