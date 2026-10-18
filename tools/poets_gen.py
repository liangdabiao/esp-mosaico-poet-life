#!/usr/bin/env python3
"""Generate the C data tables for the poets-survival game.

Reads data/*.json (produced by tools/extract_data.js from the original
single-file HTML game) and emits main/generated/poets_data.{h,c}.

Usage:
    python tools/poets_gen.py [--data data] [--out main/generated]
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

STAT_KEYS = ["silver", "food", "fame", "health", "muse", "friends", "poems"]

CATEGORY_NAMES = {
    "epitaph": "墓志铭",
    "recommend": "干谒常识",
    "teaching": "经典典籍",
    "banquet": "宴饮文化",
    "poetry": "诗词格律",
    "travel": "地理名胜",
    "farming": "农事田园",
    "official": "官制文史",
    "art": "书画艺术",
    "business": "古代商业",
    "libai": "诗人作品",
    "dufu": "诗人作品",
    "baijuyi": "诗人作品",
    "sushi": "诗人作品",
    "menghaoran": "诗人作品",
    "taoyuanming": "诗人作品",
}

# Common actions, in the original order. cat: earn/social/craft/"" (none);
# conds mirror the source conditions; quiz is the question pool key.
ACTIONS = [
    {"label": "当官办公", "desc": "答对俸禄更多", "cat": "", "quiz": "official",
     "cond": {"need_rank": True}},
    {"label": "干谒求荐", "desc": "答对收益翻倍", "cat": "earn", "quiz": "recommend",
     "cond": {"min_fame": 20, "min_friends": 10}},
    {"label": "写墓志铭", "desc": "答对润笔翻倍", "cat": "earn", "quiz": "epitaph",
     "cond": {"min_fame": 30}},
    {"label": "卖字画", "desc": "答对卖价更高", "cat": "earn", "quiz": "art",
     "cond": {}},
    {"label": "教书授课", "desc": "答对束修更多", "cat": "", "quiz": "teaching",
     "cond": {}},
    {"label": "经商买卖", "desc": "答对稳赚不赔", "cat": "", "quiz": "business",
     "cond": {"min_silver": 30}},
    {"label": "宴饮交友", "desc": "答对人脉大增", "cat": "social", "quiz": "banquet",
     "cond": {"min_silver": 20}},
    {"label": "参加诗会", "desc": "答对名声大涨", "cat": "social", "quiz": "poetry",
     "cond": {"min_silver": 10}},
    {"label": "闭门作诗", "desc": "答对可出佳作", "cat": "craft", "quiz": "poetry",
     "cond": {"min_muse": 15}},
    {"label": "游历天下", "desc": "答对收获丰盛", "cat": "", "quiz": "travel",
     "cond": {"min_silver": 30, "min_food": 15}},
    {"label": "隐居务农", "desc": "答对收成更好", "cat": "", "quiz": "farming",
     "cond": {}},
    {"label": "静养休息", "desc": "体魄+15", "cat": "", "quiz": "", "cond": {}},
]

SPECIAL_QUIZ = {"libai", "dufu", "baijuyi", "sushi", "menghaoran", "taoyuanming"}

# Event choices whose effect is a quiz callback in the original JS (functions
# are dropped by JSON extraction). Transcribed verbatim from index.html.
QUIZ_EVENT_CHOICES = {
    ("灵感迸发", "即兴赋诗一首"): {
        "quiz": "poetry",
        "quiz_ok": {"fame": 18, "poems": 1, "muse": -12},
        "quiz_ok_result": "你即兴赋诗，文思泉涌！这首诗后来成了你的得意之作。",
        "quiz_bad": {"fame": 8, "muse": -18},
        "quiz_bad_result": "你写了首诗，但灵感来得快去得也快，诗作平平。",
    },
    ("润笔订单", "接！有钱不赚王八蛋"): {
        "quiz": "epitaph",
        "quiz_ok": {"silver": 80, "fame": 5, "muse": -8},
        "quiz_ok_result": "你才思敏捷，写出一篇精彩绝伦的墓志铭！家属感激涕零，润笔丰厚。",
        "quiz_bad": {"silver": 40, "muse": -14},
        "quiz_bad_result": "你勉强写完了墓志铭。家属觉得还行，但没什么出彩之处。",
    },
    ("诗会邀请", "盛装出席"): {
        "quiz": "poetry",
        "quiz_ok": {"silver": -10, "fame": 22, "muse": 10},
        "quiz_ok_result": "你在诗会上技惊四座！连主办官员都对你刮目相看，名声大振。",
        "quiz_bad": {"silver": -10, "fame": 8, "muse": 5},
        "quiz_bad_result": "你在诗会上表现平平，没引起什么关注。",
    },
    ("书商来访", "同意出版"): {
        "quiz": "poetry",
        "quiz_ok": {"silver": 55, "fame": 18},
        "quiz_ok_result": "你的诗集大受欢迎！洛阳纸贵，你的名声响彻文坛。",
        "quiz_bad": {"silver": 25, "fame": 8},
        "quiz_bad_result": "诗集出版了，但销量一般。书商说下次再合作。",
    },
    ("皇帝寿辰", "精心创作贺寿诗"): {
        "quiz": "poetry",
        "quiz_ok": {"fame": 28, "poems": 1, "muse": -15},
        "quiz_ok_result": "你的贺寿诗文采飞扬！皇帝龙颜大悦，赏金千两！",
        "quiz_bad": {"fame": 10, "muse": -22},
        "quiz_bad_result": "你绞尽脑汁写了一首贺寿诗，但未能引起注意。",
    },
    ("画作被赏识", "卖了换钱"): {
        "quiz": "art",
        "quiz_ok": {"silver": 60, "fame": 5, "muse": -3},
        "quiz_ok_result": "买主对你的画赞不绝口，出了个高价！你的画名也因此传开。",
        "quiz_bad": {"silver": 35, "muse": -8},
        "quiz_bad_result": "你卖了这幅画。价格还不错，但总觉得亏了。",
    },
}


def c_str(text: str) -> str:
    if text is None:
        return '""'
    out = text.replace("\\", "\\\\").replace('"', '\\"')
    return '"' + out + '"'


def effect_init(eff: dict) -> str:
    """C initializer for poets_effect_t from a JSON effect dict."""
    vals = [int(eff.get(k, 0)) for k in STAT_KEYS]
    return "{" + ", ".join(str(v) for v in vals) + "}"


def effect_is_empty(eff: dict) -> bool:
    return all(int(eff.get(k, 0)) == 0 for k in STAT_KEYS)


def gen_events(events: list, special: dict) -> tuple[str, str]:
    """Returns (declarations, definitions) fragments for all events."""
    decls, defs = [], []
    for idx, ev in enumerate(events):
        name = f"event_{idx}"
        choices = []
        for cidx, c in enumerate(ev["choices"]):
            cname = f"{name}_choice_{cidx}"
            eff = c.get("effect")
            patched = QUIZ_EVENT_CHOICES.get((ev["title"], c["label"]))
            if patched is not None:
                cdecl = (
                    f"static const poets_choice_t {cname} = {{\n"
                    f"    .label = {c_str(c['label'])},\n"
                    f"    .quiz_category = POETS_CAT_{patched['quiz'].upper()},\n"
                    f"    .quiz_ok = {effect_init(patched['quiz_ok'])},\n"
                    f"    .quiz_bad = {effect_init(patched['quiz_bad'])},\n"
                    f"    .quiz_ok_result = {c_str(patched['quiz_ok_result'])},\n"
                    f"    .quiz_bad_result = {c_str(patched['quiz_bad_result'])},\n"
                    "};"
                )
            elif isinstance(eff, dict):
                cdecl = (
                    f"static const poets_choice_t {cname} = {{\n"
                    f"    .label = {c_str(c['label'])},\n"
                    # 必须显式写 NONE：省略会被 C 零初始化成 0，而 0 是
                    # POETS_CAT_EPITAPH（NONE 是 -1），平选项会被当成答题项。
                    f"    .quiz_category = POETS_CAT_NONE,\n"
                    f"    .effect = {effect_init(eff)},\n"
                    f"    .result = {c_str(c.get('result', ''))},\n"
                    "};"
                )
            else:  # function effect with no transcription: fail loudly
                raise SystemExit(
                    f"untranscribed function effect in '{ev['title']}' / "
                    f"'{c['label']}'")
            choices.append((cname, cdecl))
        decls.extend(cdecl for _, cdecl in choices)
        arr = "static const poets_choice_t *const %s_choices[] = {%s};\n" % (
            name, ", ".join("&" + cname for cname, _ in choices))
        edef = (
            f"{arr}"
            f"const poets_event_t {name} = {{\n"
            f"    .title = {c_str(ev['title'])},\n"
            f"    .text = {c_str(ev['text'])},\n"
            f"    .choice_count = {len(choices)},\n"
            f"    .choices = {name}_choices,\n"
            "};"
        )
        defs.append(edef)
    return "\n".join(decls), "\n\n".join(defs)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", default="data")
    parser.add_argument("--out", default="main/generated")
    args = parser.parse_args()

    data_dir = Path(args.data)
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    poets = json.loads((data_dir / "poets.json").read_text(encoding="utf-8"))
    eras = json.loads((data_dir / "eras.json").read_text(encoding="utf-8"))
    events = json.loads((data_dir / "events.json").read_text(encoding="utf-8"))
    special = json.loads((data_dir / "specialEvents.json").read_text(encoding="utf-8"))
    bank = json.loads((data_dir / "questionBank.json").read_text(encoding="utf-8"))

    # ---- question pools: 10 common categories + 6 per-poet, stable order ----
    pool_order = ["epitaph", "recommend", "teaching", "banquet", "poetry",
                  "travel", "farming", "official", "art", "business",
                  "libai", "dufu", "baijuyi", "sushi", "menghaoran",
                  "taoyuanming"]
    cat_enum = "\n".join(
        f"#define POETS_CAT_{key.upper()} {i}" for i, key in enumerate(pool_order))

    pool_defs = []
    pool_refs = []
    total_q = 0
    for pi, key in enumerate(pool_order):
        pool = bank[key]
        total_q += len(pool)
        rows = ",\n".join(
            "    {.q = %s, .options = {%s}, .answer = %d, .explain = %s, .recite = %s}" % (
                c_str(x["q"]),
                ", ".join(c_str(o) for o in x["options"]),
                int(x["answer"]),
                c_str(x.get("explain", "")),
                c_str(x.get("recite")),
            )
            for x in pool)
        pool_defs.append(
            "static const poets_question_t pool_%s[] = {\n%s\n};" % (key, rows))
        pool_refs.append("    {.name = %s, .count = %d, .questions = pool_%s}," % (
            c_str(CATEGORY_NAMES[key]), len(pool), key))

    # ---- events (common 18 + special appended) ----
    special_events_flat = []
    for poet_id in pool_order[10:]:
        for ev in special.get(poet_id, []):
            special_events_flat.append(ev)
    all_events = list(events) + special_events_flat
    special_start = len(events)
    ev_decls, ev_defs = gen_events(all_events, special)

    event_refs = [f"    &event_{i}," for i in range(len(all_events))]

    # ---- poets ----
    poet_defs = []
    poet_refs = []
    for i, p in enumerate(poets):
        s = p["stats"]
        poet_defs.append(
            "const poets_poet_t poet_%s = {\n"
            "    .id = %s,\n    .name = %s,\n    .tagline = %s,\n"
            "    .era = %s,\n    .desc = %s,\n    .special = %s,\n"
            "    .special_desc = %s,\n    .story = %s,\n    .rank = %s,\n"
            "    .stats = {.silver=%d, .food=%d, .fame=%d, .health=%d, .muse=%d, .friends=%d},\n"
            "    .initial_poems = %d,\n    .has_rank = %s,\n    .special_action = %s,\n"
            "};" % (
                p["id"], c_str(p["id"]), c_str(p["name"]), c_str(p["tagline"]),
                c_str(p["era"]), c_str(p["desc"]), c_str(p["special"]),
                c_str(p["specialDesc"]), c_str(p["story"]), c_str(p["rank"]),
                int(s["silver"]), int(s["food"]), int(s["fame"]), int(s["health"]),
                int(s["muse"]), int(s["friends"]),
                int(p.get("poems", 0)),
                "true" if p["rank"] not in ("布衣", "前彭泽令") else "false",
                "true" if p["id"] in SPECIAL_QUIZ else "false",
            ))
        poet_refs.append(f"    &poet_{p['id']},")

    era_rows = ",\n".join(
        "    {%s}" % ", ".join(c_str(e) for e in era) for era in eras)
    months = [["正月", "三月", "五月", "七月", "九月"],
              ["二月", "四月", "六月", "八月", "十月"],
              ["三月", "六月", "九月", "十一月", "腊月"]]
    month_rows = ",\n".join(
        "    {%s}" % ", ".join(c_str(m) for m in group) for group in months)

    action_rows = []
    for a in ACTIONS:
        cond = a["cond"]
        action_rows.append(
            "    {.label = %s, .desc = %s, .category = POETS_ACTION_CAT_%s,\n"
            "     .quiz_category = %s,\n"
            "     .min_silver = %d, .min_food = %d, .min_fame = %d,\n"
            "     .min_friends = %d, .min_muse = %d, .need_rank = %s}," % (
                c_str(a["label"]), c_str(a["desc"]), a["cat"].upper() or "NONE",
                ("POETS_CAT_" + a["quiz"].upper()) if a["quiz"] else "POETS_CAT_NONE",
                int(cond.get("min_silver", 0)), int(cond.get("min_food", 0)),
                int(cond.get("min_fame", 0)), int(cond.get("min_friends", 0)),
                int(cond.get("min_muse", 0)),
                "true" if cond.get("need_rank") else "false"))

    header = f"""// SPDX-License-Identifier: Apache-2.0
// Generated by tools/poets_gen.py from data/*.json. Do not edit by hand.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {{
#endif

#define POETS_POET_COUNT      {len(poets)}
#define POETS_EVENT_COUNT     {len(all_events)}
#define POETS_SPECIAL_EVENT_START  {special_start}
#define POETS_POOL_COUNT      {len(pool_order)}
#define POETS_ACTION_COUNT    {len(ACTIONS)}
#define POETS_MAX_TURNS       15
#define POETS_TOTAL_QUESTIONS {total_q}

/* Category indices into g_pools (question pools), stable order. */
{cat_enum}
#define POETS_CAT_NONE (-1)

typedef struct {{
    int16_t silver, food, fame, health, muse, friends, poems;
}} poets_effect_t;

typedef struct {{
    const char *id, *name, *tagline, *era, *desc, *special, *special_desc,
               *story, *rank;
    struct {{ int16_t silver, food, fame, health, muse, friends; }} stats;
    uint8_t initial_poems;
    bool has_rank;
    bool special_action;
}} poets_poet_t;

typedef struct {{
    const char *q;
    const char *options[4];
    uint8_t answer;
    const char *explain;
    const char *recite;   /* NULL when the question has no recitation */
}} poets_question_t;

typedef struct {{
    const char *name;     /* display name of the category */
    uint8_t count;
    const poets_question_t *questions;
}} poets_question_pool_t;

typedef struct {{
    const char *label;
    poets_effect_t effect;        /* flat effect; zero when quiz choice */
    const char *result;
    int quiz_category;            /* POETS_CAT_NONE when not a quiz choice */
    poets_effect_t quiz_ok;
    poets_effect_t quiz_bad;
    const char *quiz_ok_result;
    const char *quiz_bad_result;
}} poets_choice_t;

typedef struct {{
    const char *title, *text;
    uint8_t choice_count;
    const poets_choice_t *const *choices;
}} poets_event_t;

typedef enum {{
    POETS_ACTION_CAT_NONE = 0,
    POETS_ACTION_CAT_EARN,
    POETS_ACTION_CAT_SOCIAL,
    POETS_ACTION_CAT_CRAFT,
}} poets_action_cat_t;

typedef struct {{
    const char *label, *desc;
    poets_action_cat_t category;
    int quiz_category;            /* POETS_CAT_NONE when not a quiz action */
    int16_t min_silver, min_food, min_fame, min_friends, min_muse;
    bool need_rank;
}} poets_action_def_t;

extern const poets_poet_t *const g_poets[POETS_POET_COUNT];
extern const poets_event_t *const g_events[POETS_EVENT_COUNT];
extern const poets_question_pool_t g_pools[POETS_POOL_COUNT];
extern const poets_action_def_t g_actions[POETS_ACTION_COUNT];
extern const char *const g_eras[3][6];
extern const char *const g_months[3][5];

const poets_poet_t *poets_poet_by_id(const char *id);

#ifdef __cplusplus
}}
#endif
"""

    body = f"""// SPDX-License-Identifier: Apache-2.0
// Generated by tools/poets_gen.py from data/*.json. Do not edit by hand.

#include <stddef.h>

#include "generated/poets_data.h"

{''.join(chr(10).join(pool_defs) + chr(10) for pool_defs in [pool_defs])}

const poets_question_pool_t g_pools[POETS_POOL_COUNT] = {{
{chr(10).join(pool_refs)}
}};

{ev_decls}

{ev_defs}

const poets_event_t *const g_events[POETS_EVENT_COUNT] = {{
{chr(10).join(event_refs)}
}};

{chr(10).join(poet_defs)}

const poets_poet_t *const g_poets[POETS_POET_COUNT] = {{
{chr(10).join(poet_refs)}
}};

const poets_action_def_t g_actions[POETS_ACTION_COUNT] = {{
{chr(10).join(action_rows)}
}};

const char *const g_eras[3][6] = {{
{era_rows}
}};

const char *const g_months[3][5] = {{
{month_rows}
}};

const poets_poet_t *poets_poet_by_id(const char *id)
{{
    for (int i = 0; i < POETS_POET_COUNT; i++) {{
        const poets_poet_t *p = g_poets[i];
        if (p->id[0] == id[0] && __builtin_strcmp(p->id, id) == 0) {{
            return p;
        }}
    }}
    return NULL;
}}
"""

    (out_dir / "poets_data.h").write_text(header, encoding="utf-8")
    (out_dir / "poets_data.c").write_text(body, encoding="utf-8")
    print(f"poets_gen: {len(poets)} poets, {len(all_events)} events "
          f"({special_start} common), {total_q} questions, {len(ACTIONS)} actions")
    print(f"poets_gen: {out_dir / 'poets_data.h'} and .c written")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
