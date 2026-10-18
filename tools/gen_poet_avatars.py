#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""诗人头像烘焙：用 PIL 画 6 位诗人 40x40 肖像，输出游程 span 到
main/generated/pl_avatar_data.{c,h}。

每位诗人共用墨色（发/眉/须）与肤色，袍色与巾帽色各自一套，所以调色板按人
存放。设备端不做逐像素贴图：每行同色的连续像素合成一个 span，绘制时一次
DrawRectangle，和字模走同一条快路径。

用法：python tools/gen_poet_avatars.py
"""
from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
OUT_DIR = ROOT / "main" / "generated"

W = 40
H = 40
COLS = 5  # 0 透明 / 1 墨 / 2 肤 / 3 袍 / 4 巾帽

INK = (30, 26, 24)
SKIN = (232, 198, 158)

# 每位诗人：(袍色, 巾帽色)。巾帽是身份标识，袍色负责一眼区分六张脸。
POETS = [
    ("李白", (104, 136, 168), (58, 74, 100)),
    ("杜甫", (86, 118, 92), (122, 92, 58)),
    ("白居易", (152, 110, 80), (94, 78, 66)),
    ("苏轼", (96, 112, 140), (44, 48, 62)),
    ("孟浩然", (126, 128, 118), (86, 90, 82)),
    ("陶渊明", (168, 150, 92), (128, 104, 62)),
]


def new_canvas(robe: tuple[int, int, int], hat: tuple[int, int, int]):
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    return img, d, {1: INK, 2: SKIN, 3: robe, 4: hat}


def body_and_face(d, idx):
    """颈 → 袍（肩线在 y=26，别让头飘着）→ 交领 → 脸 → 发际 → 眉眼。"""
    robe = idx[3]
    skin = idx[2]
    ink = idx[1]
    d.rectangle([17, 22, 22, 27], fill=skin)
    d.polygon([(4, H - 1), (4, 33), (13, 26), (27, 26), (36, 33), (36, H - 1)], fill=robe)
    d.line([(16, 27), (20, 32), (24, 27)], fill=ink, width=1)  # 交领
    d.ellipse([11, 8, 29, 26], fill=skin)                      # 脸
    d.rectangle([12, 8, 28, 10], fill=ink)                     # 发际
    d.rectangle([16, 15, 17, 16], fill=ink)                    # 左眼
    d.rectangle([22, 15, 23, 16], fill=ink)                    # 右眼


def hat_libai(d, idx):
    """李白：软巾垂带，长髯。"""
    hat = idx[4]
    d.ellipse([12, 1, 28, 12], fill=hat)
    d.rectangle([18, 0, 21, 3], fill=hat)
    d.line([(12, 10), (8, 17)], fill=hat, width=2)
    d.polygon([(14, 21), (20, 25), (26, 21), (23, 34), (17, 34)], fill=idx[1])


def hat_dufu(d, idx):
    """杜甫：展脚幞头，短髯。"""
    hat = idx[4]
    d.rectangle([12, 2, 27, 8], fill=hat)
    d.rectangle([3, 5, 11, 6], fill=hat)
    d.rectangle([28, 5, 36, 6], fill=hat)
    d.rectangle([15, 22, 24, 27], fill=idx[1])


def hat_baijuyi(d, idx):
    """白居易：小巾无髯。"""
    hat = idx[4]
    d.rectangle([13, 4, 26, 8], fill=hat)
    d.rectangle([17, 1, 22, 4], fill=hat)
    d.rectangle([18, 21, 21, 21], fill=idx[1])


def hat_sushi(d, idx):
    """苏轼：高筒东坡巾，络腮长髯。"""
    hat = idx[4]
    d.rectangle([13, 0, 26, 8], fill=hat)
    d.rectangle([11, 8, 28, 9], fill=hat)
    d.rectangle([13, 21, 26, 25], fill=idx[1])
    d.polygon([(15, 25), (25, 25), (21, 33), (18, 33)], fill=idx[1])


def hat_menghaoran(d, idx):
    """孟浩然：圆顶布巾，只留八字须。"""
    hat = idx[4]
    d.ellipse([12, 3, 28, 10], fill=hat)
    d.rectangle([15, 20, 24, 20], fill=idx[1])   # 只留八字须，再画嘴就成了两张口


def hat_taoyuanming(d, idx):
    """陶渊明：斗笠宽檐，短髯。"""
    hat = idx[4]
    d.polygon([(20, 2), (38, 13), (2, 13)], fill=hat)
    d.rectangle([2, 13, 37, 14], fill=hat)
    d.rectangle([15, 23, 24, 26], fill=idx[1])


HATS = [hat_libai, hat_dufu, hat_baijuyi, hat_sushi, hat_menghaoran, hat_taoyuanming]
MAX_SPANS = 900


def to_spans(img, robe, hat):
    """逐行同色游程 → (x, y, w, color)。透明像素跳过。"""
    palette = {INK: 1, SKIN: 2, robe: 3, hat: 4}
    px = img.load()
    spans = []
    for y in range(H):
        x = 0
        while x < W:
            color = px[x, y]
            if color[3] == 0:
                x += 1
                continue
            idx = palette.get(color[:3])
            if idx is None:
                raise SystemExit(f"像素 ({x},{y}) 颜色 {color[:3]} 不在调色板里")
            run = 1
            while x + run < W and px[x + run, y] == color:
                run += 1
            spans.append((x, y, run, idx))
            x += run
    if len(spans) > MAX_SPANS:
        raise SystemExit(f"游程数 {len(spans)} 超过上限 {MAX_SPANS}")
    return spans


def main() -> None:
    data = []
    palettes = []
    images = []
    for (name, robe, hat), hat_fn in zip(POETS, HATS):
        img, d, idx = new_canvas(robe, hat)
        body_and_face(d, idx)
        hat_fn(d, idx)
        data.append((name, to_spans(img, robe, hat)))
        palettes.append(((0, 0, 0), INK, SKIN, robe, hat))
        images.append(img)

    h = []
    h.append("// SPDX-License-Identifier: Apache-2.0")
    h.append("// 由 tools/gen_poet_avatars.py 生成，请勿手改。")
    h.append("#pragma once")
    h.append("")
    h.append("#include <stdint.h>")
    h.append("")
    h.append(f"#define PL_AVATAR_W {W}")
    h.append(f"#define PL_AVATAR_H {H}")
    h.append(f"#define PL_AVATAR_COLORS {COLS}")
    h.append(f"#define PL_AVATAR_COUNT {len(data)}")
    h.append("")
    h.append("typedef struct {")
    h.append("    uint8_t x;      /* 相对头像左边缘的像素列 */")
    h.append("    uint8_t y;")
    h.append("    uint8_t w;      /* 同色水平游程长度 */")
    h.append("    uint8_t color;  /* pl_avatar_palette[诗人][color] */")
    h.append("} pl_avatar_span_t;")
    h.append("")
    h.append("typedef struct { uint8_t r, g, b; } pl_avatar_rgb_t;")
    h.append("")
    h.append("extern const pl_avatar_rgb_t pl_avatar_palette[PL_AVATAR_COUNT][PL_AVATAR_COLORS];")
    h.append("extern const pl_avatar_span_t *const pl_avatar_spans[PL_AVATAR_COUNT];")
    h.append("extern const uint16_t pl_avatar_span_count[PL_AVATAR_COUNT];")
    (OUT_DIR / "pl_avatar_data.h").write_text("\n".join(h) + "\n", encoding="utf-8")

    c = []
    c.append("// SPDX-License-Identifier: Apache-2.0")
    c.append("// 由 tools/gen_poet_avatars.py 生成，请勿手改。")
    c.append('#include "pl_avatar_data.h"')
    c.append("")
    c.append("const pl_avatar_rgb_t pl_avatar_palette[PL_AVATAR_COUNT][PL_AVATAR_COLORS] = {")
    for (name, _), pal in zip(data, palettes):
        cells = ", ".join(f"{{{r}, {g}, {b}}}" for (r, g, b) in pal)
        c.append(f"    {{ {cells} }},  /* {name} */")
    c.append("};")
    c.append("")
    for i, (name, spans) in enumerate(data):
        c.append(f"static const pl_avatar_span_t avatar_{i}[{len(spans)}] = {{")
        for x, y, w, color in spans:
            c.append(f"    {{{x}, {y}, {w}, {color}}},")
        c.append("};")
        c.append("")
    c.append("const pl_avatar_span_t *const pl_avatar_spans[PL_AVATAR_COUNT] = {")
    for i in range(len(data)):
        c.append(f"    avatar_{i},")
    c.append("};")
    c.append("")
    counts = ", ".join(str(len(spans)) for _, spans in data)
    c.append(f"const uint16_t pl_avatar_span_count[PL_AVATAR_COUNT] = {{ {counts} }};")
    (OUT_DIR / "pl_avatar_data.c").write_text("\n".join(c) + "\n", encoding="utf-8")

    total = sum(len(spans) for _, spans in data)
    print(f"生成 {len(data)} 位诗人头像，共 {total} 个游程，"
          f"最多 {max(len(spans) for _, spans in data)}（上限 {MAX_SPANS}）")

    # python tools/gen_poet_avatars.py --preview build-host/avatars.png
    # 深色底＋横向摆动三格，用来目视确认头像在标题页上的观感。
    import sys

    if "--preview" in sys.argv:
        out = Path(sys.argv[sys.argv.index("--preview") + 1])
        out.parent.mkdir(parents=True, exist_ok=True)
        sheet = Image.new("RGBA", (W * 3 * 6 + 20, H + 8), (28, 24, 20, 255))
        for col, img in enumerate(images):
            for k, dx in enumerate((-3, 0, 3)):
                sheet.paste(img, (10 + (col * 3 + k) * W + dx, 4), img)
        sheet.save(out)
        print(f"预览图：{out}")


if __name__ == "__main__":
    main()
