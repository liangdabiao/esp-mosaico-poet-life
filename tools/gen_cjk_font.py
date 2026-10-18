#!/usr/bin/env python3
"""生成 poet_life 用的中英文位图字模子集（main/pl_cjk_glyph_data.h）。

raylib-lite 的 DrawText 只有 5x7 ASCII，不支持 CJK。本工具用 Pillow 从
Noto Sans CJK SC 母字体把「编译进固件的全部文案字符」光栅化成 24x24 位图，
打包成按码点升序排列的 C 表，由 main/pl_font.c 做二分查找 + 游程 blit。

与 tamaweb_pet 的同名工具相比，本项目文案量差两个数量级（《诗与远方》的
事件、题目、解释、称号全是成句中文），所以这里有三处不同：

  * 单字 24x24、每行 32bit（16x16 一行放不下）；
  * ASCII 也进同一张表并带可变宽度 advance，中英文能在一条字符串里混排；
  * 扫描源包含生成的内容表 main/generated/poets_data.c —— 题库与事件文案
    就住在那里，缺一个字节都不是「少个字」而是渲染时凭空消失。

字模清单：
  * UI_STRINGS（本文件维护的界面文案）；
  * SCAN_SOURCES 里全部 C 字符串字面量；
  * data/*.json（内容改动后尚未重新生成 C 表时也不缺字）。

母字体缺失的码点会直接报错退出 —— 宁可在生成期失败，也不要在设备上留空白框。

用法（项目根目录）：
    python tools/gen_cjk_font.py          # 写出字模表
    python tools/gen_cjk_font.py --check  # 只校验是否与源码同步
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "main" / "pl_cjk_glyph_data.h"
OUT_C = ROOT / "main" / "pl_cjk_glyph_data.c"
SCAN_SOURCES = [
    ROOT / "main" / "game.c",
    ROOT / "main" / "poets_game.c",
    ROOT / "main" / "generated" / "poets_data.c",
    ROOT / "main" / "pl_text.c",
]
SCAN_DATA = sorted((ROOT / "data").glob("*.json"))
FONT_PATH = Path("D:/esp/fontsrc/NotoSansCJKsc-Regular.otf")

# 只在本文件或 UI 代码里出现、不在上面源码里的文案（若有）也列在这里。
UI_STRINGS: list[str] = [
    "《诗与远方》", "古代诗人生存手册", "再来一局", "生涯履历", "称号图鉴",
]

CELL = 24
SIZE = 24
ASCII_FIRST, ASCII_LAST = 0x20, 0x7E
MIN_ADVANCE = 5

CJK_PUNCT = "·『』、，。（）：；？％！“”‘’—…～《》【】"

EXTRA_RANGES = (
    (0x00B7, 0x00B7),
    (0x00D7, 0x00D7),
    (0x2010, 0x206F),
    (0x2460, 0x24FF),
    (0x25A0, 0x25FF),
)

STRING_LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')


def is_ascii(ch: str) -> bool:
    return ASCII_FIRST <= ord(ch) <= ASCII_LAST


def is_wanted(ch: str) -> bool:
    o = ord(ch)
    if is_ascii(ch):
        return ch != '"' and ch != "\\"        # 只烘实际会用到的排版字符
    if o >= 0x2E80 or ch in CJK_PUNCT:
        return True
    return any(lo <= o <= hi for lo, hi in EXTRA_RANGES)


def literals_of(path: Path) -> list[str]:
    if not path.is_file():
        return []
    text = path.read_text(encoding="utf-8")
    return [m.group(1) for m in STRING_LITERAL.finditer(text)]


def collect_chars() -> list[str]:
    chars: set[str] = set()
    for s in UI_STRINGS:
        chars.update(c for c in s if is_wanted(c))
    for src in SCAN_SOURCES:
        for lit in literals_of(src):
            chars.update(c for c in lit if is_wanted(c))
    for data in SCAN_DATA:
        for c in data.read_text(encoding="utf-8"):
            if is_wanted(c):
                chars.add(c)
    return sorted(chars)


def missing_from_font(chars: list[str]) -> list[str]:
    from fontTools.ttLib import TTFont
    with TTFont(str(FONT_PATH), fontNumber=0, lazy=True) as tt:
        cmap = set(tt.getBestCmap().keys())
    return [c for c in chars if ord(c) not in cmap]


def rasterize(font, ch: str) -> tuple[int, list[int]]:
    """返回 (advance, rows)。ASCII 左对齐并按墨迹宽度给步进，其余居中占满一格。"""
    from PIL import Image, ImageDraw
    im = Image.new("L", (CELL * 2, CELL * 2), 0)
    d = ImageDraw.Draw(im)
    d.text((CELL // 2, CELL // 2), ch, font=font, fill=255)
    bbox = im.getbbox()
    if bbox is None:                                   # 空格一类无墨字符
        return (CELL // 2 if is_ascii(ch) else CELL, [0] * CELL)
    left, top, right, bottom = bbox
    ink_w = right - left
    ink_h = bottom - top
    cropped = im.crop((left, top, right, bottom))
    canvas = Image.new("L", (CELL, CELL), 0)
    if is_ascii(ch):
        advance = max(MIN_ADVANCE, ink_w + 2)
        ox = 0
        oy = max(0, (CELL - ink_h) // 2 - 1)
    else:
        advance = CELL
        ox = max(0, (CELL - ink_w) // 2)
        oy = max(0, (CELL - ink_h) // 2)
    canvas.paste(cropped, (ox, oy))
    px = canvas.load()
    rows = []
    for y in range(CELL):
        bits = 0
        for x in range(CELL):
            if px[x, y] > 96:
                bits |= 1 << (CELL - 1 - x)
        rows.append(bits)
    return advance, rows


def generate() -> tuple[str, str]:
    if not FONT_PATH.is_file():
        raise SystemExit(f"找不到母字体：{FONT_PATH}")
    chars = collect_chars()
    missing = missing_from_font(chars)
    if missing:
        raise SystemExit(
            "母字体缺少这些码点：" + " ".join(f"{c}(U+{ord(c):04X})" for c in missing)
        )
    from PIL import ImageFont
    font = ImageFont.truetype(str(FONT_PATH), SIZE)
    glyphs = [(ch,) + rasterize(font, ch) for ch in chars]

    header = [
        "// SPDX-License-Identifier: Apache-2.0",
        "// 由 tools/gen_cjk_font.py 自动生成 —— 勿手改。",
        f"// 母字体：Noto Sans CJK SC Regular（SIL OFL 1.1）；{len(chars)} 个字模，"
        f"{CELL}x{CELL} 单色，按码点升序（pl_font.c 二分查找）。",
        "// 表体在 pl_cjk_glyph_data.c：头文件里的 static 数组会让每个编译单元",
        "// 各自复制一份 600+KB 字模，还会让跨文件的指针比较必然不相等。",
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        f"#define PL_FONT_CELL {CELL}",
        f"#define PL_FONT_SIZE {SIZE}",
        "",
        "typedef struct {",
        "    uint32_t cp;",
        f"    uint32_t rows[PL_FONT_CELL];   /* bit (PL_FONT_CELL-1-x) = 第 x 列 */",
        "    uint8_t advance;               /* 中文一格，ASCII 按墨迹宽度 */",
        "} pl_font_glyph_t;",
        "",
        f"#define PL_FONT_GLYPH_COUNT {len(glyphs)}",
        f"extern const pl_font_glyph_t PL_FONT_GLYPHS[PL_FONT_GLYPH_COUNT];",
        "",
    ]
    body = [
        header[0],
        header[1],
        "// 24x24 单色位图字模表，按码点升序。生成命令见文件头注释。",
        '#include "pl_cjk_glyph_data.h"',
        "",
        f"const pl_font_glyph_t PL_FONT_GLYPHS[PL_FONT_GLYPH_COUNT] = {{",
    ]
    for ch, advance, rows in glyphs:
        rows_str = ",".join(f"0x{r:08x}" for r in rows)
        label = ch if ch != "\\" else "\\\\"
        body.append(f"    {{ 0x{ord(ch):04x}, {{ {rows_str} }}, {advance} }},  /* {label} */")
    body.append("};")
    body.append("")
    return "\n".join(header) + "\n", "\n".join(body) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="只校验生成物是否与源码同步，过期就失败")
    args = parser.parse_args()
    head_text, body_text = generate()
    if args.check:
        for path, text in ((OUT, head_text), (OUT_C, body_text)):
            if not path.is_file() or path.read_text(encoding="utf-8") != text:
                print(f"STALE: {path.name} —— 字符清单已变化，请重跑 gen_cjk_font.py",
                      file=sys.stderr)
                return 1
        print(f"up to date: {OUT.name}, {OUT_C.name}")
        return 0
    OUT.write_text(head_text, encoding="utf-8", newline="\n")
    OUT_C.write_text(body_text, encoding="utf-8", newline="\n")
    count = body_text.count("    { 0x")
    cjk = sum(1 for m in re.finditer(r"    \{ 0x([0-9a-f]{4}),", body_text)
              if int(m.group(1), 16) > 0x2E80)
    print(f"wrote {OUT.name} + {OUT_C.name}: {count} glyphs ({cjk} CJK), "
          f"{count * (CELL * CELL // 8)} bytes bitmap")
    return 0


if __name__ == "__main__":
    sys.exit(main())
