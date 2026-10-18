// SPDX-License-Identifier: Apache-2.0
// 一次性诊断：测试串里哪些码点没进字模表。编译见 tests/build_tests.ps1。
#include <stdio.h>
#include <string.h>

#include "pl_text.h"
#include "pl_cjk_glyph_data.h"

static void scan(const char *label, const char *s)
{
    printf("== %s ==\n", label);
    size_t i = 0;
    while (s[i]) {
        uint32_t cp = 0;
        i += pl_font_utf8_next(&s[i], &cp);
        if (pl_font_find(cp) == NULL) {
            printf("  MISSING U+%04X\n", cp);
        }
    }
}

static void layout_report(const char *label, const char *s, int w)
{
    pl_text_layout_t lay;
    pl_text_layout(s, w, &lay);
    printf("== %s (w=%d) total=%d count=%d ==\n", label, w, lay.total, lay.count);
    for (int k = 0; k < lay.count; k++) {
        printf("  [%2d] w=%3d  %.*s\n", k, lay.lines[k].width,
               (int)(lay.lines[k].end - lay.lines[k].begin), s + lay.lines[k].begin);
    }
}

int main(void)
{
    scan("forty", "种豆南山下草盛豆苗稀种豆南山下草盛豆苗稀"
                  "种豆南山下草盛豆苗稀种豆南山下草盛豆苗稀");
    scan("punct", "你写得一手好墓志铭家属赞不绝口润笔八十两，但是诗兴枯竭了名声也受损了，");
    scan("nums", "一二三四五六七八九十润笔187两当前回合3/15名声大振");
    scan("saturation", "干谒求荐写墓志铭卖字画诗");
    scan("measure", "你是诗圣杜甫。生逢安史之乱，半生颠沛。小儿子饿死，你自己也常常断粮。");

    char punct[4096];
    strcpy(punct, "你写得一手好墓志铭家属赞不绝口润笔八十两，");
    for (int k = 0; k < 6; k++) strcat(punct, "但是诗兴枯竭了名声也受损了，");
    layout_report("punct", punct, 24 * 12);

    layout_report("nums", "一二三四五六七八九十润笔187两当前回合3/15名声大振", 24 * 9);

    static char big[4096];
    big[0] = '\0';
    for (int k = 0; k < 42; k++) strcat(big, "干谒求荐写墓志铭卖字画诗");
    layout_report("saturation", big, 24 * 10);

    big[0] = '\0';
    for (int k = 0; k < 20; k++) strcat(big, "种豆南山下草盛豆苗稀");
    layout_report("pages-20", big, 24 * 10);
    return 0;
}
