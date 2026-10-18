// SPDX-License-Identifier: Apache-2.0
// 宿主单元测试：规则核心不变量 + 全数据完整性 + 确定性重放。
// 编译（w64devkit gcc）：见 tests/build_tests.ps1。
#include <stdio.h>
#include <string.h>

#include "poets_game.h"
#include "pl_save.h"

enum { RESULT_BUF = 320 };

static int failures;
static int checks;
static uint16_t s_base_poems;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        checks++;                                                            \
        if (!(cond)) {                                                       \
            printf("FAIL: %s\n", msg);                                       \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static void test_data_integrity(void)
{
    for (int i = 0; i < POETS_POOL_COUNT; i++) {
        const poets_question_pool_t *pool = &g_pools[i];
        CHECK(pool->count > 0, "pool non-empty");
        for (int q = 0; q < pool->count; q++) {
            const poets_question_t *x = &pool->questions[q];
            CHECK(x->answer >= 0 && x->answer <= 3, "answer in range");
            for (int o = 0; o < 4; o++) CHECK(x->options[o] != NULL, "option set");
            CHECK(x->explain != NULL, "explain set");
        }
    }
    for (int i = 0; i < POETS_EVENT_COUNT; i++) {
        const poets_event_t *e = g_events[i];
        CHECK(e->choice_count == 2, "every event has exactly 2 choices");
        for (int c = 0; c < e->choice_count; c++) {
            const poets_choice_t *ch = e->choices[c];
            CHECK(ch->label != NULL, "choice label set");
            if (ch->quiz_category != POETS_CAT_NONE) {
                CHECK(ch->quiz_category >= 0 && ch->quiz_category < POETS_POOL_COUNT,
                      "quiz category in range");
            }
        }
    }
    for (int i = 0; i < POETS_POET_COUNT; i++) {
        const poets_poet_t *p = g_poets[i];
        CHECK(p->stats.silver >= 0 && p->stats.food >= 0, "initial stats sane");
        CHECK(p->special != NULL && p->special[0] != '\0', "poet has special");
    }
}

static void test_action_availability(void)
{
    // 布衣/前彭泽令 no rank: 当官办公 (index 0) must be disabled.
    for (int i = 0; i < POETS_POET_COUNT; i++) {
        poets_game_t g;
        poets_game_init(&g, g_poets[i], 42);
        bool expect_rank = g_poets[i]->has_rank;
        CHECK(poets_game_action_enabled(&g, 0) == expect_rank,
              "office action gated by rank");
    }
    // 经商买卖 (5) requires 30 silver: dufu starts at 20 → disabled.
    {
        poets_game_t g;
        poets_game_init(&g, g_poets[1], 42); // dufu
        CHECK(g_poets[1]->stats.silver == 20, "dufu silver precondition");
        CHECK(!poets_game_action_enabled(&g, 5), "business gated by silver");
    }
}

static void test_death_and_clamp(void)
{
    poets_game_t g;
    poets_game_init(&g, g_poets[0], 7); // libai, silver 80
    // Food drains 10 per month (no farming): starvation kills eventually.
    for (int i = 0; i < 20 && !g.dead; i++) {
        char log[256];
        poets_game_process_month(&g, log, sizeof(log));
    }
    CHECK(g.food >= 0, "food never negative");
    CHECK(g.health >= 0 && g.health <= 100, "health clamped");
    CHECK(g.dead, "starvation eventually kills");
    CHECK(strcmp(poets_death_reason_name(g.death_reason), "饿死") == 0,
          "starvation death reason");
}

static void test_full_runs(void)
{
    // Every poet, several seeds: play out with a greedy-but-varied policy and
    // assert end-to-end invariants + deterministic replay.
    for (int p = 0; p < POETS_POET_COUNT; p++) {
        for (uint32_t seed = 1; seed <= 5; seed++) {
            poets_game_t g;
            poets_game_init(&g, g_poets[p], seed);
            char log[256];
            int quiz_answered = 0;
            int t = 0;
            while (!g.dead) {
                if (!poets_game_advance_turn(&g)) break;
                // pick the first enabled action, rotating for variety
                int n = poets_game_action_count(&g);
                int chosen = -1;
                uint32_t policy = seed * 1000u + (uint32_t)t;
                for (int k = 0; k < n; k++) {
                    int idx =
                        (int)(poets_game_rand(&policy) % (uint32_t)n);
                    if (poets_game_action_enabled(&g, idx)) {
                        chosen = idx;
                        break;
                    }
                }
                if (chosen < 0) break;
                int quiz = poets_game_action_quiz(&g, chosen);
                bool correct = (poets_game_rand(&policy) % 100) < 50;
                poets_game_resolve_action(&g, chosen, correct, log, sizeof(log));
                CHECK(log[0] != '\0', "action produced a log line");
                quiz_answered += (quiz != POETS_CAT_NONE);
                if (g.dead) break;
                poets_game_process_month(&g, log, sizeof(log));
                if (g.dead) break;
                const poets_event_t *ev = poets_game_pick_event(&g);
                CHECK(ev != NULL && ev->choice_count == 2, "event picked");
                poets_game_apply_choice(&g, ev->choices[correct ? 0 : 1],
                                        correct, log, sizeof(log));
                CHECK(!g.dead || g.death_reason < 6, "death reason valid");
                t++;
            }
            CHECK(g.dead || g.turn > POETS_MAX_TURNS, "run terminates");
            CHECK(g.fame >= 0 && g.fame <= 100, "fame clamped");
            CHECK(g.health >= 0 && g.health <= 100, "health clamped");
            CHECK(g.silver >= 0 && g.food >= 0, "no negative balance");

            poets_ending_t ending;
            poets_game_ending(&g, &ending);
            CHECK(ending.title < (uint8_t)poets_ending_title_count(),
                  "ending title valid");
            CHECK(ending.score >= 0, "score non-negative");
            (void)quiz_answered;
        }
    }
}

static void test_determinism(void)
{
    // Same seed → identical final state across two runs.
    poets_game_t a, b;
    poets_game_init(&a, g_poets[0], 2026);
    poets_game_init(&b, g_poets[0], 2026);
    char log[256];
    for (int t = 0; t < POETS_MAX_TURNS && !a.dead && !b.dead; t++) {
        (void)poets_game_advance_turn(&a);
        (void)poets_game_advance_turn(&b);
        poets_game_resolve_action(&a, 2, true, log, sizeof(log));
        poets_game_resolve_action(&b, 2, true, log, sizeof(log));
        poets_game_process_month(&a, log, sizeof(log));
        poets_game_process_month(&b, log, sizeof(log));
        const poets_event_t *ea = poets_game_pick_event(&a);
        const poets_event_t *eb = poets_game_pick_event(&b);
        CHECK(ea == eb, "same event picked");
        poets_game_apply_choice(&a, ea->choices[0], true, log, sizeof(log));
        poets_game_apply_choice(&b, eb->choices[0], true, log, sizeof(log));
    }
    CHECK(memcmp(&a, &b, sizeof(a)) == 0, "deterministic replay");
}

/* ------------------------------------------------------------------ P1 扩展
 * 下面几组是「点名要数值」的断言：分支走没走、乘子算成几分、死因/称号落到哪格。
 * 基线状态一律 silver/food=1000、四项有界维度=50，好让 apply_effect 的钳位规则
 * （银粮不透支、有界维度 0..100）可以在测试里用同一个公式复算。 */

static int16_t clamp_expect(int16_t base, int16_t delta)
{
    int32_t v = (int32_t)base + delta;
    if (v > 100) v = 100;
    if (v < 0) v = 0;
    return (int16_t)v;
}

static void boot_baseline(poets_game_t *g)
{
    poets_game_init(g, g_poets[0], 1234);
    g->dead = false;
    g->silver = 1000;
    g->food = 1000;
    g->fame = 50;
    g->health = 50;
    g->muse = 50;
    g->friends = 50;
    s_base_poems = g->poems;
}

static int effect_is_zero(const poets_effect_t *e)
{
    return e->silver == 0 && e->food == 0 && e->fame == 0 && e->health == 0
        && e->muse == 0 && e->friends == 0 && e->poems == 0;
}

static void assert_effect(const poets_effect_t *e, const poets_game_t *g,
                          const char *what)
{
    CHECK(g->silver == 1000 + e->silver, "silver delta");
    CHECK(g->food == 1000 + e->food, "food delta");
    CHECK(g->fame == clamp_expect(50, e->fame), what);
    CHECK(g->health == clamp_expect(50, e->health), "health delta");
    CHECK(g->muse == clamp_expect(50, e->muse), "muse delta");
    CHECK(g->friends == clamp_expect(50, e->friends), "friends delta");
    CHECK(g->poems == (uint16_t)(s_base_poems + e->poems), "poems delta");
}

/* 平选项绝不能走答题分支：曾经因为生成器省略 quiz_category（C 零初始化成 0，
 * 而 0 恰好是 EPITAPH）把每个平选项都当答题项，设备上直接显示 (null)。 */
static void test_choice_branches(void)
{
    int flat = 0, quiz = 0;
    for (int i = 0; i < POETS_EVENT_COUNT; i++) {
        const poets_event_t *ev = g_events[i];
        CHECK(ev->title != NULL && ev->title[0] != '\0', "event title set");
        CHECK(ev->text != NULL && ev->text[0] != '\0', "event text set");
        for (int c = 0; c < ev->choice_count; c++) {
            char a[RESULT_BUF], b[RESULT_BUF];
            const poets_choice_t *ch = ev->choices[c];
            poets_game_t g;
            if (ch->quiz_category == POETS_CAT_NONE) {
                flat++;
                CHECK(ch->result != NULL && ch->result[0] != '\0',
                      "flat choice carries result");
                CHECK(effect_is_zero(&ch->quiz_ok) && effect_is_zero(&ch->quiz_bad),
                      "flat choice carries no quiz effects");
                boot_baseline(&g);
                poets_game_apply_choice(&g, ch, true, a, sizeof(a));
                assert_effect(&ch->effect, &g, "flat choice applied (correct)");
                boot_baseline(&g);
                poets_game_apply_choice(&g, ch, false, b, sizeof(b));
                assert_effect(&ch->effect, &g, "flat choice applied (wrong)");
                CHECK(strcmp(a, b) == 0, "flat choice ignores quiz answer");
                CHECK(strcmp(a, ch->result) == 0, "flat choice result text");
            } else {
                quiz++;
                CHECK(ch->quiz_category >= 0 && ch->quiz_category < POETS_POOL_COUNT,
                      "quiz choice category in pool range");
                CHECK(g_pools[ch->quiz_category].count > 0,
                      "quiz choice pool non-empty");
                CHECK(ch->quiz_ok_result != NULL && ch->quiz_bad_result != NULL,
                      "quiz choice carries both results");
                boot_baseline(&g);
                poets_game_apply_choice(&g, ch, true, a, sizeof(a));
                assert_effect(&ch->quiz_ok, &g, "quiz choice ok effect");
                CHECK(strcmp(a, ch->quiz_ok_result) == 0, "quiz ok result text");
                boot_baseline(&g);
                poets_game_apply_choice(&g, ch, false, b, sizeof(b));
                assert_effect(&ch->quiz_bad, &g, "quiz choice bad effect");
                CHECK(strcmp(b, ch->quiz_bad_result) == 0, "quiz bad result text");
                CHECK(strcmp(a, b) != 0, "quiz branches tell different stories");
            }
        }
    }
    CHECK(flat > 0 && quiz > 0, "both choice kinds exist in the content table");
}

/* 行动门槛：每一项 min_* 都是「差 1 就锁、到值就开」，need_rank 单独一档。 */
static void test_action_gates(void)
{
    for (int p = 0; p < POETS_POET_COUNT; p++) {
        poets_game_t g;
        poets_game_init(&g, g_poets[p], 7);
        const int n = poets_game_action_count(&g);
        CHECK(n == POETS_ACTION_COUNT + (g_poets[p]->special_action ? 1 : 0),
              "action count follows special_action");
        for (int i = 0; i < n; i++) {
            const poets_action_def_t d = *poets_game_action_def(&g, i);
            CHECK(d.label[0] != '\0', "action label set");
            CHECK(d.desc[0] != '\0', "action desc set");
            CHECK(d.category <= POETS_ACTION_CAT_CRAFT, "action category in range");
            CHECK(d.quiz_category == POETS_CAT_NONE
                      || (d.quiz_category >= 0 && d.quiz_category < POETS_POOL_COUNT),
                  "action quiz category in range");
            CHECK(poets_game_action_quiz(&g, i) == d.quiz_category,
                  "action_quiz mirrors the def");

            boot_baseline(&g);
            g.has_rank = true;
            g.silver = 9999;
            g.food = 9999;
            CHECK(poets_game_action_enabled(&g, i), "unlocked action is enabled");
            if (d.need_rank) {
                g.has_rank = false;
                CHECK(!poets_game_action_enabled(&g, i), "rank gates the action");
                g.has_rank = true;
            }
            const int16_t *mins[5] = {&d.min_silver, &d.min_food, &d.min_fame,
                                      &d.min_friends, &d.min_muse};
            int16_t *stats[5] = {&g.silver, &g.food, &g.fame, &g.friends, &g.muse};
            for (int k = 0; k < 5; k++) {
                if (*mins[k] <= 0) continue;
                const int16_t keep = *stats[k];
                *stats[k] = (int16_t)(*mins[k] - 1);
                CHECK(!poets_game_action_enabled(&g, i), "one short of the gate");
                *stats[k] = *mins[k];
                CHECK(poets_game_action_enabled(&g, i), "exactly at the gate");
                *stats[k] = keep;
            }
        }
    }
}

/* 六个死因各自对应一维见底，且死人不能再行动。 */
static void test_death_reasons(void)
{
    static const char *const names[6] = {"穷死", "饿死", "臭死", "弱死", "傻死", "闷死"};
    for (int i = 0; i < 6; i++) {
        poets_game_t g;
        boot_baseline(&g);
        g.has_rank = false;              /* 别让俸禄把银两救回来 */
        int16_t *stats[6] = {&g.silver, &g.food, &g.fame, &g.health, &g.muse,
                             &g.friends};
        *stats[i] = 0;
        char log[RESULT_BUF];
        poets_game_process_month(&g, log, sizeof(log));
        CHECK(g.dead, "zeroed stat kills");
        CHECK(g.death_reason == i, "death reason index matches the stat");
        CHECK(strcmp(poets_death_reason_name(g.death_reason), names[i]) == 0,
              "death reason name");
        CHECK(poets_death_reason_text(g.death_reason)[0] != '\0',
              "death reason text set");
        for (int a = 0; a < poets_game_action_count(&g); a++) {
            CHECK(!poets_game_action_enabled(&g, a), "a corpse takes no action");
        }
    }
    CHECK(strcmp(poets_death_reason_name(6), "?") == 0, "out-of-range name is '?'");
    CHECK(poets_death_reason_text(6)[0] == '\0', "out-of-range text is empty");
}

/* 无 RNG 的行动分支：数值必须逐字对上原版的固定收益表。 */
static void test_action_deltas(void)
{
    poets_game_t g;
    char log[RESULT_BUF];

    boot_baseline(&g);              /* 0 当官办公：答对 35/5/-3 */
    g.has_rank = true;
    poets_game_resolve_action(&g, 0, true, log, sizeof(log));
    CHECK(g.silver == 1035 && g.fame == 55 && g.muse == 47, "office success");
    CHECK(log[0] != '\0', "office success narrative");
    boot_baseline(&g);
    g.has_rank = true;
    poets_game_resolve_action(&g, 0, false, log, sizeof(log));
    CHECK(g.silver == 1020 && g.fame == 52 && g.muse == 45, "office failure");

    boot_baseline(&g);              /* 4 教书授课：答对 25/5/-3/-1 */
    poets_game_resolve_action(&g, 4, true, log, sizeof(log));
    CHECK(g.silver == 1025 && g.fame == 55 && g.muse == 47 && g.health == 49,
          "teaching success");
    boot_baseline(&g);
    poets_game_resolve_action(&g, 4, false, log, sizeof(log));
    CHECK(g.silver == 1015 && g.muse == 45 && g.health == 48, "teaching failure");

    boot_baseline(&g);              /* 5 经商买卖：投入按银两封顶 */
    g.silver = 20;
    poets_game_resolve_action(&g, 5, true, log, sizeof(log));
    CHECK(g.silver >= 20 && g.silver <= 20 + 40 * 87 / 100, "business profit bounds");
    boot_baseline(&g);
    g.silver = 0;
    poets_game_resolve_action(&g, 5, true, log, sizeof(log));
    CHECK(g.silver == 0, "no silver, no investment");
}

/* 乘子表：连续同类 ×0.85、人脉≥40 ×1.3，两项在宴饮上是纯整数、可逐值核对。 */
static void test_multipliers(void)
{
    poets_game_t g;
    char log[RESULT_BUF];
    const int social_action = 6;
    boot_baseline(&g);
    const int cat = poets_game_action_def(&g, social_action)->category;
    CHECK(cat == POETS_ACTION_CAT_SOCIAL, "banquet is a social action");

    struct { int repeat; int friends; int fr; int mu; } cases[4] = {
        {0, 30, 15, 12},   /* 1000‰ */
        {0, 60, 19, 15},   /* 1300‰ */
        {1, 30, 12, 10},   /* 850‰ */
        {1, 60, 16, 13},   /* 850‰×1300‰ = 1105‰ */
    };
    for (size_t i = 0; i < 4; i++) {
        boot_baseline(&g);
        g.friends = (int16_t)cases[i].friends;
        g.last_category = cases[i].repeat ? (uint8_t)cat : 0;
        const int base_friends = g.friends;
        poets_game_resolve_action(&g, social_action, true, log, sizeof(log));
        CHECK(g.silver == 980, "banquet always costs 20 silver");
        CHECK(g.friends == base_friends + cases[i].fr, "social friends gain");
        CHECK(g.muse == clamp_expect(50, cases[i].mu), "social muse gain");
    }

    /* 诗兴枯竭（<20）时收益减半：同种子同行动，低诗兴不得高于高诗兴。 */
    int low = 0, high = 0;
    for (uint32_t seed = 1; seed <= 20; seed++) {
        poets_game_init(&g, g_poets[0], seed);
        g.silver = 1000; g.food = 1000; g.fame = 50; g.health = 50; g.muse = 10;
        poets_game_resolve_action(&g, 3, true, log, sizeof(log));   /* 卖字画 */
        low += g.silver;
        poets_game_init(&g, g_poets[0], seed);
        g.silver = 1000; g.food = 1000; g.fame = 50; g.health = 50; g.muse = 90;
        poets_game_resolve_action(&g, 3, true, log, sizeof(log));
        high += g.silver;
    }
    CHECK(low < high, "muse multiplier really halves the gain");

    for (int i = 0; i < POETS_ACTION_COUNT; i++) {
        boot_baseline(&g);
        g.has_rank = true;
        char ok[RESULT_BUF], bad[RESULT_BUF];
        poets_game_resolve_action(&g, i, true, ok, sizeof(ok));
        boot_baseline(&g);
        g.has_rank = true;
        poets_game_resolve_action(&g, i, false, bad, sizeof(bad));
        CHECK(ok[0] != '\0' && bad[0] != '\0', "both branches narrate");
    }
}

/* 九档结局称号：每档都要能被单独构造出来，得分公式逐位核对。 */
static void test_endings(void)
{
    const int count = poets_ending_title_count();
    CHECK(count == 9, "nine ending titles");
    for (int i = 0; i < count; i++) {
        CHECK(poets_ending_title(i)[0] != '\0', "title set");
        CHECK(poets_ending_subtitle(i)[0] != '\0', "subtitle set");
        CHECK(poets_ending_seal(i)[0] != '\0', "seal set");
    }
    static const struct {
        int16_t silver, food, fame, health, muse, friends; uint16_t poems;
        uint8_t title;
    } cases[9] = {
        {10, 10, 90, 50, 10, 10, 5, 0},      /* 诗坛巨匠 */
        {300, 200, 10, 50, 10, 10, 0, 1},    /* 富贵达人 */
        {10, 10, 10, 50, 10, 80, 0, 2},      /* 人脉广布 */
        {10, 10, 10, 10, 10, 10, 0, 3},      /* 悲剧诗人 */
        {10, 10, 10, 50, 80, 10, 4, 4},      /* 诗兴盎然 */
        {10, 160, 10, 50, 10, 10, 0, 5},     /* 丰衣足食 */
        {120, 10, 50, 50, 10, 10, 0, 6},     /* 安稳文人 */
        {10, 10, 10, 50, 10, 10, 0, 7},      /* 寒酸书生 */
        {50, 10, 30, 50, 10, 10, 0, 8},      /* 平凡一生 */
    };
    for (size_t i = 0; i < 9; i++) {
        poets_game_t g;
        poets_ending_t e;
        boot_baseline(&g);
        g.silver = cases[i].silver;
        g.food = cases[i].food;
        g.fame = cases[i].fame;
        g.health = cases[i].health;
        g.muse = cases[i].muse;
        g.friends = cases[i].friends;
        g.poems = cases[i].poems;
        g.has_rank = false;
        poets_game_ending(&g, &e);
        CHECK(e.title == cases[i].title, "ending title branch");
        const int32_t cap_silver = cases[i].silver < 200 ? cases[i].silver : 200;
        const int32_t cap_food = cases[i].food < 150 ? cases[i].food : 150;
        const int32_t cap_friends = cases[i].friends < 50 ? cases[i].friends : 50;
        CHECK(e.score == (cap_silver * 5 + cap_food * 3 + cases[i].fame * 15
                          + cases[i].poems * 200 + cap_friends * 5 + 5) / 10,
              "ending score formula");
    }
    /* 银两/粮食/人脉在封顶值以上不再加分，名声与诗作封顶之外继续吃分。 */
    poets_game_t g;
    poets_ending_t a, b;
    boot_baseline(&g);
    g.silver = 200; g.food = 150; g.friends = 50; g.fame = 100; g.poems = 0;
    poets_game_ending(&g, &a);
    g.silver = 9999; g.food = 9999; g.friends = 9999;
    poets_game_ending(&g, &b);
    CHECK(a.score == b.score, "capped stats stop paying out");
    g.poems = 1;
    poets_game_ending(&g, &b);
    CHECK(b.score > a.score, "every extra poem is worth 20");
}

/* 存档字节层：往返一致 + 四种坏 blob 必须被拒（设备重启后不能读回半条生涯）。 */
enum { SAVE_OFF_VERSION = 4, SAVE_OFF_CHECKSUM = 38 };

/* 测试自己重算校验和，才能证明「版本不符」是被版本门挡下的，而不是被校验和顺带拦掉。 */
static void resave_checksum(uint8_t *b)
{
    uint16_t sum = 0;
    for (size_t i = 0; i < SAVE_OFF_CHECKSUM; i++) sum = (uint16_t)(sum + b[i]);
    b[SAVE_OFF_CHECKSUM] = (uint8_t)(sum & 0xFFu);
    b[SAVE_OFF_CHECKSUM + 1] = (uint8_t)(sum >> 8);
}

static void test_save_codec(void)
{
    poets_game_t core, back;
    pl_save_data_t d, out;
    uint8_t bytes[PL_SAVE_BYTES + 4];
    memset(&core, 0, sizeof(core));
    core.poet = g_poets[2];
    core.turn = 9;
    core.silver = 1234;
    core.food = -7;                       /* 存档必须原样保留负数中间态 */
    core.fame = 88;
    core.health = 12;
    core.muse = 65;
    core.friends = 41;
    core.poems = 7;
    core.last_category = POETS_ACTION_CAT_SOCIAL;
    core.dead = false;
    core.death_reason = 3;
    core.rng = 0xDEADBEEFu;
    pl_save_capture(&d, &core, 2, PL_PAGE_HOME);
    CHECK(pl_save_encode(&d, bytes, sizeof(bytes)) == PL_SAVE_BYTES, "encode length");
    CHECK(pl_save_decode(bytes, PL_SAVE_BYTES, &out), "decode accepts its own bytes");
    CHECK(out.poet_index == 2 && out.page == PL_PAGE_HOME, "index/page survived");
    poets_game_init(&back, g_poets[0], 1);   /* 先清成另一辈子，才看得出还原是否完整 */
    pl_save_restore(&out, &back);
    CHECK(back.turn == 9 && back.silver == 1234 && back.food == -7, "stats restored");
    CHECK(back.fame == 88 && back.health == 12 && back.muse == 65
              && back.friends == 41,
          "bounded stats restored");
    CHECK(back.poems == 7 && back.rng == 0xDEADBEEFu, "poems and rng restored");
    CHECK(back.last_category == POETS_ACTION_CAT_SOCIAL && !back.dead, "flags");
    CHECK(out.death_reason == 3, "death reason survived");

    CHECK(!pl_save_decode(bytes, PL_SAVE_BYTES - 1, &out), "short blob rejected");
    CHECK(!pl_save_decode(bytes, PL_SAVE_BYTES + 1, &out), "long blob rejected");
    bytes[1] ^= 0xFF;
    CHECK(!pl_save_decode(bytes, PL_SAVE_BYTES, &out), "bad magic rejected");
    bytes[1] = 'L';
    bytes[SAVE_OFF_CHECKSUM] ^= 0x01;      /* 高位校验和字节 */
    CHECK(!pl_save_decode(bytes, PL_SAVE_BYTES, &out), "bad checksum rejected");
    bytes[SAVE_OFF_CHECKSUM] ^= 0x01;
    bytes[SAVE_OFF_VERSION] = 99;          /* 未来版本：校验和同步补正，只让版本门生效 */
    resave_checksum(bytes);
    CHECK(!pl_save_decode(bytes, PL_SAVE_BYTES, &out), "unknown version rejected");
    bytes[SAVE_OFF_VERSION] = 1;
    resave_checksum(bytes);
    CHECK(pl_save_decode(bytes, PL_SAVE_BYTES, &out), "repaired blob accepted");
    out.poet_index = POETS_POET_COUNT;     /* 越界下标在编码层就该被拒 */
    pl_save_encode(&out, bytes, sizeof(bytes));
    CHECK(!pl_save_decode(bytes, PL_SAVE_BYTES, &out), "bad poet index rejected");
    pl_save_capture(&d, &core, 0, PL_PAGE_COUNT);
    pl_save_encode(&d, bytes, sizeof(bytes));
    CHECK(!pl_save_decode(bytes, PL_SAVE_BYTES, &out), "bad page rejected");
}

static void test_era_labels(void)
{
    for (int t = 0; t < POETS_MAX_TURNS; t++) {
        const char *s = poets_game_era_label((uint8_t)t);
        CHECK(s != NULL && s[0] != '\0', "era label non-empty");
        CHECK(strstr(s, "·") != NULL, "era label carries separators");
    }
}

int main(void)
{
    test_data_integrity();
    test_action_availability();
    test_death_and_clamp();
    test_full_runs();
    test_determinism();
    test_choice_branches();
    test_action_gates();
    test_death_reasons();
    test_action_deltas();
    test_multipliers();
    test_endings();
    test_era_labels();
    test_save_codec();
    if (failures == 0) {
        printf("host_test: ALL PASSED (%d assertions)\n", checks);
        return 0;
    }
    printf("host_test: %d failure(s)\n", failures);
    return 1;
}
