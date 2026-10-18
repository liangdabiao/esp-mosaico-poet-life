#!/usr/bin/env python3
"""内容层尺寸与字模覆盖体检（写 UI 前定缓冲区、装机前查漏字）。

1. 统计 data/*.json 与 generated/poets_data.c 里各字段字符串的最长字节数，
   main/game.c 的缓冲区就按这些数字定，不靠猜。
2. 扫描 main/ 下所有 C 字符串字面量，报告字模表里查不到的码点 —— 缺字不是
   「少一个字」，而是渲染时凭空消失，必须构建期就失败。tests/ 的样本串故意
   含缺字（用来验证「缺字不画」），所以不在扫描范围里。

用法：python tools/text_metrics.py
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GEN = ROOT / "main" / "generated" / "poets_data.c"
GLYPH_SRC = ROOT / "main" / "pl_cjk_glyph_data.c"
LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')
CODEPOINT = re.compile(r"\{ 0x([0-9a-f]{4}),")


def baked_codepoints() -> set[int]:
    text = GLYPH_SRC.read_text(encoding="utf-8")
    return {int(m.group(1), 16) for m in CODEPOINT.finditer(text)}


def decode(literal: str) -> str:
    try:
        return literal.encode("utf-8").decode("unicode_escape").encode(
            "latin1").decode("utf-8")
    except (UnicodeDecodeError, UnicodeEncodeError):
        return literal


def longest_literals() -> list[tuple[int, str]]:
    out = []
    for path in [GEN, *sorted((ROOT / "data").glob("*.json"))]:
        if not path.is_file():
            continue
        for m in LITERAL.finditer(path.read_text(encoding="utf-8")):
            s = decode(m.group(1))
            out.append((len(s.encode("utf-8")), s))
    out.sort(reverse=True)
    return out


def missing_glyphs() -> list[str]:
    baked = baked_codepoints()
    missing: set[str] = set()
    # 只扫要烧进固件的 main/：tests/ 里的样本串故意含缺字，用来验证「缺字不画」。
    srcs = [*(ROOT / "main").rglob("*.c"), *(ROOT / "main").rglob("*.h")]
    for path in srcs:
        if path.name == "pl_cjk_glyph_data.c":
            continue
        for m in LITERAL.finditer(path.read_text(encoding="utf-8")):
            for ch in decode(m.group(1)):
                if ord(ch) >= 0x2E80 and ord(ch) not in baked:
                    missing.add(ch)
    return sorted(missing)


def json_field_max() -> None:
    events = json.loads((ROOT / "data" / "events.json").read_text(encoding="utf-8"))
    bank = json.loads((ROOT / "data" / "questionBank.json").read_text(encoding="utf-8"))
    poets = json.loads((ROOT / "data" / "poets.json").read_text(encoding="utf-8"))

    def collect(node, acc):
        if isinstance(node, str):
            acc.append(len(node.encode("utf-8")))
        elif isinstance(node, list):
            for v in node:
                collect(v, acc)

    def walk(node, key, acc):
        if isinstance(node, dict):
            for k, v in node.items():
                if k == key:
                    collect(v, acc)
                else:
                    walk(v, key, acc)
        elif isinstance(node, list):
            for v in node:
                walk(v, key, acc)

    for name, blob, keys in (("events", events, ("title", "text", "label", "result")),
                             ("questionBank", bank, ("q", "explain", "recite", "options")),
                             ("poets", poets, ("name", "desc", "story", "tagline"))):
        for key in keys:
            acc: list[int] = []
            walk(blob, key, acc)
            if acc:
                print(f"{name}.{key}: max {max(acc)} bytes over {len(acc)} 条")


def main() -> int:
    print("== 最长字符串（UTF-8 字节）==")
    for size, s in longest_literals()[:10]:
        print(f"  {size:5d}  {s[:48]}")
    print()
    json_field_max()
    print()
    missing = missing_glyphs()
    if missing:
        print("MISSING GLYPHS: " + " ".join(f"{c}(U+{ord(c):04X})" for c in missing),
              file=sys.stderr)
        return 1
    print("字模覆盖：main/ 字面量全部命中。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
