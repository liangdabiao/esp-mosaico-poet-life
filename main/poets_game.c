// SPDX-License-Identifier: Apache-2.0
// 《诗与远方》规则核心实现。数值公式与原版 JS 一一对应：
//   名声乘数 fameMult = 0.5 + fame/100；诗兴枯竭 muse<20 收益减半；
//   人脉红利 friends>=40 社交 +30%；连续同类行动 ×0.85。
// 原版浮点收益用两段 per-mille 整数乘法实现（逐段除 1000，防 int32 溢出），
// 与 Math.floor 语义一致。
#include "poets_game.h"

#include <stdio.h>
#include <string.h>

// ------------------------------------------------------------------ RNG

uint32_t poets_game_rand(uint32_t *state)
{
    uint32_t x = *state ? *state : 0x6D2B79F5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static uint32_t rng_next(poets_game_t *g)
{
    uint32_t x = g->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g->rng = x ? x : 0x9E3779B9u;
    return g->rng;
}

static int rng_range(poets_game_t *g, int n) // [0, n)
{
    return (int)(rng_next(g) % (uint32_t)n);
}

static int rng_percent(poets_game_t *g) // [0, 99]
{
    return (int)(rng_next(g) % 100u);
}

// ------------------------------------------------------- effect application

// Applies one effect with the original clamping: silver/food floor at the
// current value (no negative balance), the four bounded stats clamp to
// [0,100], poems unbounded. Death check runs afterwards.
static void apply_effect(poets_game_t *g, const poets_effect_t *e)
{
    if (e->silver != 0) {
        int32_t d = e->silver;
        if (d < -(int32_t)g->silver) d = -g->silver;
        g->silver = (int16_t)(g->silver + d);
    }
    if (e->food != 0) {
        int32_t d = e->food;
        if (d < -(int32_t)g->food) d = -g->food;
        g->food = (int16_t)(g->food + d);
    }
    int16_t *bounded[4] = {&g->fame, &g->health, &g->muse, &g->friends};
    const int16_t deltas[4] = {e->fame, e->health, e->muse, e->friends};
    for (int i = 0; i < 4; i++) {
        if (deltas[i] == 0) continue;
        int32_t v = (int32_t)*bounded[i] + deltas[i];
        if (v > 100) v = 100;
        if (v < 0) v = 0;
        *bounded[i] = (int16_t)v;
    }
    if (e->poems != 0) {
        g->poems = (uint16_t)(g->poems + e->poems);
    }

    if (g->dead) return;
    const int16_t stats[6] = {g->silver, g->food, g->fame,
                              g->health, g->muse, g->friends};
    /* 六维与死因一一对应：stats[i] 见底就是 deaths[i]，文案表在 s_death_* 里。 */
    for (size_t i = 0; i < 6; i++) {
        if (stats[i] <= 0) {
            g->dead = true;
            g->death_reason = (uint8_t)i;
            return;
        }
    }
}

// ------------------------------------------------------------- multipliers

typedef struct {
    int fame;   // per-mille: 500 + fame*10
    int muse;   // per-mille: 500 (枯竭) or 1000
    int repeat; // per-mille: 850 (连续同类) or 1000
    int social; // per-mille: repeat * friend_bonus
} mults_t;

static void get_mults(const poets_game_t *g, poets_action_cat_t cat, mults_t *m)
{
    m->fame = 500 + g->fame * 10;
    m->muse = g->muse < 20 ? 500 : 1000;
    m->repeat = (cat != POETS_ACTION_CAT_NONE && g->last_category == (uint8_t)cat)
                    ? 850
                    : 1000;
    m->social = m->repeat * (g->friends >= 40 ? 1300 : 1000) / 1000;
}

// Scale a positive randomized gain by two per-mille factors (two divisions,
// matching Math.floor of the original float chain).
static int scale_gain(int gain, int factor_a, int factor_b)
{
    int32_t v = gain;
    if (factor_a != 0) v = v * factor_a / 1000;
    if (factor_b != 0) v = v * factor_b / 1000;
    return (int)v;
}

// ------------------------------------------------------------- lifecycle

void poets_game_init(poets_game_t *g, const poets_poet_t *poet, uint32_t seed)
{
    memset(g, 0, sizeof(*g));
    g->poet = poet;
    g->silver = poet->stats.silver;
    g->food = poet->stats.food;
    g->fame = poet->stats.fame;
    g->health = poet->stats.health;
    g->muse = poet->stats.muse;
    g->friends = poet->stats.friends;
    g->poems = poet->initial_poems;
    g->has_rank = poet->has_rank;
    g->rng = seed ? seed : 1;
}

// --------------------------------------------------------------- actions

int poets_game_action_count(const poets_game_t *g)
{
    return POETS_ACTION_COUNT + (g->poet->special_action ? 1 : 0);
}

// Per-poet question pools follow the 10 common pools in g_pools order.
static int poet_pool_index(const poets_poet_t *poet)
{
    for (int i = 0; i < POETS_POET_COUNT; i++) {
        if (g_poets[i] == poet) return 10 + i;
    }
    return POETS_CAT_NONE;
}

static poets_action_def_t s_special_def; // single-run scratch (one caller)

const poets_action_def_t *poets_game_action_def(const poets_game_t *g, int index)
{
    if (index < POETS_ACTION_COUNT) {
        return &g_actions[index];
    }
    s_special_def.label = g->poet->special;
    s_special_def.desc = g->poet->special_desc;
    s_special_def.category = POETS_ACTION_CAT_NONE;
    s_special_def.quiz_category = poet_pool_index(g->poet);
    s_special_def.min_silver = 0;
    s_special_def.min_food = 0;
    s_special_def.min_fame = 0;
    s_special_def.min_friends = 0;
    s_special_def.min_muse = 0;
    s_special_def.need_rank = false;
    return &s_special_def;
}

bool poets_game_action_enabled(const poets_game_t *g, int index)
{
    if (g->dead) return false;
    const poets_action_def_t *a = poets_game_action_def(g, index);
    if (a->need_rank && !g->has_rank) return false;
    if (g->silver < a->min_silver) return false;
    if (g->food < a->min_food) return false;
    if (g->fame < a->min_fame) return false;
    if (g->friends < a->min_friends) return false;
    if (g->muse < a->min_muse) return false;
    return true;
}

int poets_game_action_quiz(const poets_game_t *g, int index)
{
    return poets_game_action_def(g, index)->quiz_category;
}

// Resolves one action: computes the effect exactly like the original
// (multipliers on the randomized gains only), applies it, and writes the
// narrative log line.
void poets_game_resolve_action(poets_game_t *g, int index, bool correct,
                               char *log, size_t log_size)
{
    mults_t mults;
    get_mults(g, poets_game_action_def(g, index)->category, &mults);
    poets_effect_t e = {0};
    int a = 0, gain = 0;

    switch (index) {
    case 0: // 当官办公: quiz official, fixed deltas
        if (correct) { e = (poets_effect_t){.silver = 35, .fame = 5, .muse = -3}; }
        else { e = (poets_effect_t){.silver = 20, .fame = 2, .muse = -5}; }
        snprintf(log, log_size, correct ? "公务处理得当，俸禄之外还有打点！"
                                        : "公务办得一般，俸禄照领。");
        break;
    case 1: // 干谒求荐
        if (correct) {
            a = 60 + rng_range(g, 90);
            gain = scale_gain(a, mults.fame, mults.muse * mults.repeat / 1000);
            e = (poets_effect_t){.silver = (int16_t)gain, .fame = 5, .muse = -5,
                                 .friends = -10};
            snprintf(log, log_size,
                     "干谒大获成功！获得资助 %d 两，但欠下人情（人脉-10）", gain);
        } else if (rng_percent(g) > 60 || g->fame > 60) {
            a = 40 + rng_range(g, 60);
            gain = scale_gain(a, mults.fame, mults.muse * mults.repeat / 1000);
            e = (poets_effect_t){.silver = (int16_t)gain, .fame = 2, .muse = -8,
                                 .friends = -10};
            snprintf(log, log_size, "干谒成功，获得资助 %d 两，欠下人情（人脉-10）",
                     gain);
        } else {
            e = (poets_effect_t){.fame = -10, .muse = -8, .friends = -10};
            snprintf(log, log_size,
                     "干谒失败！颜面尽失，名声受损（名声-10），还白白欠了人情");
        }
        break;
    case 2: // 写墓志铭
        if (correct) {
            a = 70 + rng_range(g, 60);
            gain = scale_gain(a, mults.fame, mults.muse);
            e = (poets_effect_t){.silver = (int16_t)gain, .fame = 5, .muse = -8};
            snprintf(log, log_size, "你写得一手好墓志铭！家属赞不绝口，润笔 %d 两",
                     gain);
        } else {
            a = 30 + rng_range(g, 40);
            gain = scale_gain(a, mults.fame, mults.muse);
            e = (poets_effect_t){.silver = (int16_t)gain, .muse = -14};
            snprintf(log, log_size, "墓志铭写得中规中矩，得润笔 %d 两", gain);
        }
        break;
    case 3: // 卖字画
        if (correct) {
            a = 30 + rng_range(g, 50);
            gain = scale_gain(a, mults.fame, mults.muse);
            e = (poets_effect_t){.silver = (int16_t)gain, .fame = 3, .muse = -6};
            snprintf(log, log_size, "你的画作被识货之人高价买走！得 %d 两", gain);
        } else {
            a = 15 + rng_range(g, 35);
            gain = scale_gain(a, mults.fame, mults.muse);
            e = (poets_effect_t){.silver = (int16_t)gain, .muse = -10};
            snprintf(log, log_size, "卖出一幅字画，得 %d 两", gain);
        }
        break;
    case 4: // 教书授课
        if (correct) {
            e = (poets_effect_t){.silver = 25, .fame = 5, .muse = -3, .health = -1};
            snprintf(log, log_size, "你讲学深入浅出，弟子们听得入迷！");
        } else {
            e = (poets_effect_t){.silver = 15, .muse = -5, .health = -2};
            snprintf(log, log_size, "一堂课讲下来，口干舌燥。");
        }
        break;
    case 5: { // 经商买卖
        if (correct) {
            int invest = g->silver < 40 ? g->silver : 40;
            int profit = (int)((int64_t)invest * (500 + rng_percent(g) * 8) / 1000);
            e = (poets_effect_t){.silver = (int16_t)profit};
            snprintf(log, log_size, "你眼光独到！生意大赚 %d 两！", profit);
        } else if (rng_percent(g) > 50) {
            int invest = g->silver < 30 ? g->silver : 30;
            int profit = (int)((int64_t)invest * (300 + rng_percent(g) * 7) / 1000);
            e = (poets_effect_t){.silver = (int16_t)profit};
            snprintf(log, log_size, "生意不错，赚了 %d 两", profit);
        } else {
            int invest = g->silver < 30 ? g->silver : 30;
            int loss = (int)((int64_t)invest * (200 + rng_percent(g) * 4) / 1000);
            e = (poets_effect_t){.silver = (int16_t)-loss};
            snprintf(log, log_size, "生意亏损了 %d 两...", loss);
        }
        break;
    }
    case 6: // 宴饮交友
        if (correct) {
            int fr = 15 * mults.social / 1000;
            int mu = 12 * mults.social / 1000;
            e = (poets_effect_t){.silver = -20, .friends = (int16_t)fr,
                                 .muse = (int16_t)mu};
            snprintf(log, log_size, "你酒令行得好，宾主尽欢！人脉+%d 诗兴+%d", fr, mu);
        } else {
            int fr = 8 * mults.social / 1000;
            e = (poets_effect_t){.silver = -20, .friends = (int16_t)fr, .muse = 8,
                                 .health = -3};
            snprintf(log, log_size, "宴饮结束，宾主还算尽兴。");
        }
        break;
    case 7: // 参加诗会
        if (correct) {
            a = 15 + rng_range(g, 11);
            int f = a * mults.social / 1000;
            int mu = 10 * mults.social / 1000;
            e = (poets_effect_t){.silver = -10, .fame = (int16_t)f,
                                 .muse = (int16_t)mu};
            snprintf(log, log_size, "你在诗会上技惊四座！名声+%d", f);
        } else {
            a = 5 + rng_range(g, 11);
            int f = a * mults.social / 1000;
            e = (poets_effect_t){.silver = -10, .fame = (int16_t)f, .muse = 6};
            snprintf(log, log_size, "诗会上你表现平平，名声+%d", f);
        }
        break;
    case 8: // 闭门作诗（mult = muse × repeat，无名声乘数）
        if (correct) {
            a = 15 + rng_range(g, 11);
            int f = scale_gain(a, mults.muse, mults.repeat);
            e = (poets_effect_t){.fame = (int16_t)f, .muse = -18, .poems = 1};
            snprintf(log, log_size, "你写出了一首传世之作！名声+%d", f);
        } else {
            a = 5 + rng_range(g, 11);
            int f = scale_gain(a, mults.muse, mults.repeat);
            e = (poets_effect_t){.fame = (int16_t)f, .muse = -22};
            snprintf(log, log_size, "你写了一首诗，但不太满意。名声+%d", f);
        }
        break;
    case 9: // 游历天下
        if (correct) {
            a = 18 + rng_range(g, 11);
            e = (poets_effect_t){.silver = -30, .food = -15, .fame = (int16_t)a,
                                 .muse = 18};
            snprintf(log, log_size, "你的游历收获满满，眼界大开！名声+%d", a);
        } else {
            a = 8 + rng_range(g, 11);
            e = (poets_effect_t){.silver = -30, .food = -15, .fame = (int16_t)a,
                                 .muse = 12, .health = -5};
            snprintf(log, log_size, "游历归来，略有收获。名声+%d", a);
        }
        break;
    case 10: // 隐居务农
        if (correct) {
            e = (poets_effect_t){.food = 35, .fame = -10, .health = 6, .muse = -10,
                                 .friends = -10};
            snprintf(log, log_size, "种瓜得瓜！今年收成不错！");
        } else {
            e = (poets_effect_t){.food = 18, .fame = -10, .health = 4, .muse = -10,
                                 .friends = -10};
            snprintf(log, log_size, "种豆南山下，草盛豆苗稀……");
        }
        break;
    case 11: // 静养休息（无答题）
        e = (poets_effect_t){.health = 15, .muse = 10};
        snprintf(log, log_size, "你睡了个体息觉，身心的疲惫一扫而空。体魄+15 诗兴+10");
        break;
    default: { // 诗人专属技能（quiz 为该诗人的作品池）
        int pi = index - POETS_ACTION_COUNT; // 0..5
        static const poets_effect_t ok_e[6] = {
            {.silver = -8, .fame = 30, .health = -3, .muse = 30},
            {.fame = 18, .health = -3, .muse = 20, .poems = 1},
            {.silver = 40},
            {.silver = -8, .health = 18, .muse = 15},
            {.fame = 8, .food = 8, .muse = 20},
            {.fame = 8, .muse = 22, .poems = 1},
        };
        static const poets_effect_t bad_e[6] = {
            {.silver = -12, .fame = 15, .health = -6, .muse = 20},
            {.fame = 8, .health = -6, .muse = 12},
            {.silver = 20},
            {.silver = -12, .health = 10, .muse = 8},
            {.fame = 3, .food = 3, .muse = 12},
            {.fame = 3, .muse = 15},
        };
        static const char *const ok_log[6] = {
            "醉后诗百篇！篇篇都是传世之作！",
            "你忧国忧民，写下千古名篇！",
            "你精打细算，产业收益颇丰！",
            "你研究出一道绝世美味！身心俱佳！",
            "在山水中独坐，诗兴如泉涌。",
            "采菊东篱下，悠然见南山。精神得到了升华。",
        };
        static const char *const bad_log[6] = {
            "醉后作诗，但质量一般。",
            "你忧国忧民，但写得有些沉重。",
            "你打理了产业，小有收益。",
            "你做了道菜，味道还行。",
            "在山中待了一天，心旷神怡。",
            "采了菊花，但没写出什么好诗。",
        };
        if (pi < 0 || pi >= 6) {
            snprintf(log, log_size, "无事发生");
            return;
        }
        e = correct ? ok_e[pi] : bad_e[pi];
        snprintf(log, log_size, "%s", correct ? ok_log[pi] : bad_log[pi]);
        break;
    }
    }

    apply_effect(g, &e);
}

// ------------------------------------------------------- question / events

const poets_question_t *poets_game_pick_question(poets_game_t *g, int category)
{
    if (category < 0 || category >= POETS_POOL_COUNT) {
        return NULL;
    }
    const poets_question_pool_t *pool = &g_pools[category];
    if (pool->count == 0) {
        return NULL;
    }
    return &pool->questions[rng_range(g, pool->count)];
}

const poets_event_t *poets_game_pick_event(poets_game_t *g)
{
    if (rng_percent(g) < 25) {
        // 特殊事件段按 g_poets 顺序排列，当前数据每位诗人 1 个。
        int poet_index = (int)(g->poet - g_poets[0]);
        if (poet_index >= 0 &&
            POETS_SPECIAL_EVENT_START + poet_index < POETS_EVENT_COUNT) {
            return g_events[POETS_SPECIAL_EVENT_START + poet_index];
        }
    }
    return g_events[rng_range(g, POETS_SPECIAL_EVENT_START)];
}

void poets_game_apply_choice(poets_game_t *g, const poets_choice_t *choice,
                             bool quiz_correct, char *result, size_t result_size)
{
    if (choice->quiz_category != POETS_CAT_NONE) {
        const poets_effect_t *e =
            quiz_correct ? &choice->quiz_ok : &choice->quiz_bad;
        const char *text = quiz_correct ? choice->quiz_ok_result
                                       : choice->quiz_bad_result;
        apply_effect(g, e);
        /* 内容表里少写一个字段就是 NULL：%s 传 NULL 在 Windows CRT 上打印
         * "(null)"，在设备上凭空消失。宁可退化成空串也不能读空指针。 */
        snprintf(result, result_size, "%s", text ? text : "");
        return;
    }
    apply_effect(g, &choice->effect);
    snprintf(result, result_size, "%s", choice->result ? choice->result : "");
}

// ---------------------------------------------------------------- turn flow

void poets_game_process_month(poets_game_t *g, char *log, size_t log_size)
{
    log[0] = '\0';
    if (g->dead) return;
    if (g->has_rank) {
        poets_effect_t e = {.silver = 15};
        apply_effect(g, &e);
    }
    poets_effect_t food_up = {.food = -10};
    apply_effect(g, &food_up);
    if (g->food <= 0 && !g->dead) {
        poets_effect_t penalty = {.health = -10, .food = 5};
        apply_effect(g, &penalty);
        snprintf(log, log_size, "粮食耗尽！你在忍饥挨饿……");
    }
}

bool poets_game_advance_turn(poets_game_t *g)
{
    if (g->dead) return false;
    g->turn++;
    return g->turn <= POETS_MAX_TURNS;
}

// ---------------------------------------------------------------- ending

void poets_game_ending(const poets_game_t *g, poets_ending_t *out)
{
    int32_t score_x10 = 0;
    score_x10 += (g->silver < 200 ? g->silver : 200) * 5;
    score_x10 += (g->food < 150 ? g->food : 150) * 3;
    score_x10 += g->fame * 15;
    score_x10 += g->poems * 200;
    score_x10 += (g->friends < 50 ? g->friends : 50) * 5;
    if (g->has_rank) score_x10 += 300;

    memset(out, 0, sizeof(*out));
    out->score = (score_x10 + 5) / 10;
    out->silver = g->silver;
    out->food = g->food;
    out->fame = g->fame;
    out->health = g->health;
    out->friends = g->friends;
    out->poems = g->poems;

    if (g->fame >= 85 && g->poems >= 3) out->title = 0;
    else if (g->silver >= 250 && g->food >= 150) out->title = 1;
    else if (g->friends >= 70) out->title = 2;
    else if (g->health <= 20) out->title = 3;
    else if (g->muse >= 70 && g->poems >= 3) out->title = 4;
    else if (g->food >= 150) out->title = 5;
    else if (g->fame >= 40 && g->silver >= 100) out->title = 6;
    else if (g->silver < 30 && g->fame < 30) out->title = 7;
    else out->title = 8;
}

// ------------------------------------------------------- table accessors

static const char *const s_death_names[6] = {
    "穷死", "饿死", "臭死", "弱死", "傻死", "闷死",
};
/* 排版器不做硬换行，所以这里刻意不写 \n：两句话直接连读，中文标点后不需要空格。 */
static const char *const s_death_texts[6] = {
    "囊中羞涩，一文不名。你彻底破产了，连买砚台的钱都没有。"
    "诗人穷死——古今常有，但轮到自己还是觉得憋屈。",
    "粮仓见底，锅中无米。你饿得头昏眼花，连笔都提不起来。"
    "\"入门闻号啕，幼子饥已卒\"——你终于读懂了杜甫的痛。",
    "名声扫地，人人避之不及。你写出来的诗被人拿去糊墙。"
    "诗人没了名声，就像剑客没了剑。社死，是真的会死。",
    "病骨支离，气息奄奄。长期的贫困和奔波终于拖垮了你的身体。"
    "药石无医，回天乏术。诗稿散落一地，被风吹走。",
    "文思枯竭，大脑空空。你对着白纸坐了一天又一天，一个字都写不出来。"
    "对诗人来说，写不出诗比死还难受——然后你真的就……",
    "孤家寡人，门可罗雀。没有朋友来拜访，没有书信往来。"
    "你对着墙壁说了三天话，然后安静地离开了这个世界。",
};

const char *poets_death_reason_name(uint8_t index)
{
    return index < 6 ? s_death_names[index] : "?";
}

const char *poets_death_reason_text(uint8_t index)
{
    return index < 6 ? s_death_texts[index] : "";
}

static const char *const s_titles[9] = {
    "诗坛巨匠", "富贵达人", "人脉广布", "悲剧诗人", "诗兴盎然",
    "丰衣足食", "安稳文人", "寒酸书生", "平凡一生",
};
static const char *const s_subtitles[9] = {
    "你的诗篇流传千古，照亮了千年岁月。",
    "你活得滋润，诗也写得从容。这就是人生赢家。",
    "朋友遍天下，走到哪都有人管饭。",
    "贫病交加，令人唏嘘。但你的诗是苦难中开出的花。",
    "灵感如泉涌，你活成了诗的样子。",
    "仓廪实而知诗，你过得比大多数诗人好。",
    "不算大富大贵，但日子过得去。诗在，希望在。",
    "理想很丰满，现实很骨感。但你没放弃写诗。",
    "不好不坏，普通过日子。但你还是写了几首诗。",
};
static const char *const s_seals[9] = {
    "诗圣", "富贵", "交友", "可叹", "诗兴", "温饱", "自在", "穷酸", "凡人",
};

const char *poets_ending_title(uint8_t index) { return s_titles[index]; }
const char *poets_ending_subtitle(uint8_t index) { return s_subtitles[index]; }
const char *poets_ending_seal(uint8_t index) { return s_seals[index]; }
int poets_ending_title_count(void) { return 9; }

const char *poets_game_era_label(uint8_t turn_zero_based)
{
    static char label[48];
    int group = turn_zero_based / 5;
    if (group > 2) group = 2;
    int col = turn_zero_based % 5;
    if (col > 4) col = 4;
    snprintf(label, sizeof(label), "%s · %s", g_eras[group][col],
             g_months[group][col]);
    return label;
}
