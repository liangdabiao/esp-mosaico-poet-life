// SPDX-License-Identifier: Apache-2.0
// 《诗与远方》：15 回合诗人一生。设备与 Host 共用这一份视图。
// 所有与输入相关的几何（点击区）都在 update 路径里重建，render 只读缓存 ——
// headless replay 只渲染最后一帧，放在 render 里算出来的值永远来不及参与判定。
// 同理：所有 set_title/set_msg/set_sub 也只允许在 update 路径上调用。
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "game_config.h"
#include "mosaico_raylib_fast.h"
#include "pl_avatar.h"
#include "pl_font.h"
#include "pl_sfx.h"
#include "pl_store.h"
#include "pl_text.h"
#include "poets_game.h"


/* 版面：480x480 方屏，上下各留 16px，四角都能放控件。
 * 行距 28 给正文，26 给挤不下的按钮与履历（字模 24px，2px 呼吸）。 */
enum {
    PAD = 16,
    TEXT_W = GAME_WIDTH - 2 * PAD,      /* 448 = 18 个整宽字 */
    LINE_PITCH = 28,
    TIGHT_PITCH = 26,
    HEADER_H = 40,
    STAT_TOP = 44,
    CONTENT_TOP = 128,
    BUTTON_TOP = 400,
    BUTTON_H = 56,
    ROW_H = 48,
    TAB_H = 32
};

/* 尺寸依据 tools/text_metrics.py 实测（UTF-8 字节）：
 * poets.story 238、events.result 123、explain 134、options 单条 51、q 63。 */
enum {
    PL_TITLE_MAX = 96,
    PL_MSG_MAX = 320,
    PL_SUB_MAX = 320,
    PL_OPT_MAX = 96,
    PL_LOG_MAX = 288
};
enum { PL_LOG_LINES = 12, PL_LOG_PAGE = 2 };
enum { PL_TAB_ROWS = 6 };

enum {
    Z_NONE = 0,
    Z_POET0 = 1,                    /* 1..6 诗人卡 */
    Z_START = 9,                    /* 标题页「入 生」 */
    Z_BIO_BEGIN = 10, Z_BIO_BACK = 11,
    Z_HOME_ACT = 12, Z_HOME_END = 13,
    Z_TAB0 = 16,                    /* 16..18 行动页签 */
    Z_ACT0 = 20,                    /* 20..25 当前页签内的行动行 */
    Z_BACK = 27,
    Z_OPT0 = 32,                    /* 32..35 答题选项 */
    Z_QUIZ_GO = 38,
    Z_CH0 = 40, Z_CH1 = 41, Z_EVENT_NEXT = 43,
    Z_LOG_PREV = 46, Z_LOG_NEXT = 47, Z_RESTART = 48
};
#define MAX_ZONES 34

enum { PEND_NONE = 0, PEND_ACTION, PEND_EVENT };

/* 行动分组：内核的 category 只覆盖 4 个行动（其余为 CAT_NONE），拿它当页签会漏掉
 * 大半行动，所以这里按“谋生 / 交际 / 自修”重新分组，下标即 g_actions 下标。 */
static const uint8_t s_tab_of_action[POETS_ACTION_COUNT] = {
    0, 0, 0, 0, 0, 0,   /* 当官办公 干谒求荐 写墓志铭 卖字画 教书授课 经商买卖 */
    1, 1, 2, 1, 2, 2    /* 宴饮 诗会 作诗 游历 务农 静养 */
};
static const char *const s_tab_names[3] = {"谋生", "交际", "自修"};
static const char *const s_option_tags[4] = {"A", "B", "C", "D"};

typedef struct {
    int id, x, y, w, h;
} zone_t;

typedef struct {
    char text[PL_LOG_MAX];
    pl_text_layout_t lay;
} log_entry_t;

struct game_t {
    game_config_t config;
    game_snapshot_t state;
    poets_game_t core;
    poets_ending_t ending;
    uint8_t page;
    uint8_t tab;
    uint8_t pending;            /* PEND_*：答题结束后回到哪条链 */
    uint8_t event_resolved;
    uint8_t answered;
    uint8_t answered_correct;
    int log_page;
    int selected;               /* 标题页选中的诗人，-1 = 还没选 */
    int pending_action;
    int pick_shown;             /* 已选选项下标，-1 = 还没选 */
    int quiz_category;
    const poets_choice_t *pending_choice;
    const poets_question_t *question;
    const poets_event_t *cur_event;
    int32_t tap_x, tap_y;
    bool tap_pending;
    bool prev_pressed;
    int8_t avatar_dx[POETS_POET_COUNT];   /* 标题页头像摆动偏移，prepare 算好 */
    char title[PL_TITLE_MAX];
    char msg[PL_MSG_MAX];
    char sub[PL_SUB_MAX];
    char opt[4][PL_OPT_MAX];
    pl_text_layout_t lay_msg, lay_sub, lay_opt[4];
    log_entry_t log[PL_LOG_LINES];
    int log_count;
    zone_t zones[MAX_ZONES];
    int zone_count;
};

static const Color COL_BG = {28, 24, 20, 255};
static const Color COL_PANEL = {46, 40, 34, 255};
static const Color COL_SEL = {74, 58, 40, 255};
static const Color COL_LINE = {128, 110, 86, 255};
static const Color COL_TEXT = {236, 228, 210, 255};
static const Color COL_DIM = {158, 148, 128, 255};
static const Color COL_SILVER = {255, 214, 102, 255};
static const Color COL_FOOD = {255, 168, 92, 255};
static const Color COL_FAME = {255, 118, 138, 255};
static const Color COL_HEALTH = {122, 220, 158, 255};
static const Color COL_MUSE = {150, 176, 255, 255};
static const Color COL_FRIENDS = {196, 150, 255, 255};
static const Color COL_SEAL = {178, 44, 38, 255};
static const Color COL_OK = {92, 208, 122, 255};
static const Color COL_BAD = {222, 92, 92, 255};

/* ---------------------------------------------------------- zone helpers */

static void zone_add(game_handle_t g, int id, int x, int y, int w, int h)
{
    if (g->zone_count >= MAX_ZONES) return;
    g->zones[g->zone_count++] = (zone_t){id, x, y, w, h};
}

/* 后加入的优先：叠放时小按钮赢过它所在的整行卡片。 */
static int zone_hit(game_handle_t g, int x, int y)
{
    for (int i = g->zone_count - 1; i >= 0; --i) {
        const zone_t *z = &g->zones[i];
        if (x >= z->x && x < z->x + z->w && y >= z->y && y < z->y + z->h) return z->id;
    }
    return Z_NONE;
}

/* render 用同一张表定位，几何只有一份来源，不会出现“画的和能点的不一样”。 */
static const zone_t *zone_find(const game_handle_t g, int id)
{
    for (int i = 0; i < g->zone_count; i++) {
        if (g->zones[i].id == id) return &g->zones[i];
    }
    return NULL;
}

/* ------------------------------------------------------------- text util */

static void set_vprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    vsnprintf(buf, size, fmt, ap);
}

static void set_title(game_handle_t g, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    set_vprintf(g->title, sizeof(g->title), fmt, ap);
    va_end(ap);
}

static void set_msg(game_handle_t g, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    set_vprintf(g->msg, sizeof(g->msg), fmt, ap);
    va_end(ap);
}

static void set_sub(game_handle_t g, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    set_vprintf(g->sub, sizeof(g->sub), fmt, ap);
    va_end(ap);
}

/* 把内核回写的叙事文本搬进四个通道。fmt 一律为 "%s"：内容层的字符串
 * 永远只当数据，绝不当格式串。 */
static void set_msg_plain(game_handle_t g, const char *s) { set_msg(g, "%s", s ? s : ""); }
static void set_sub_plain(game_handle_t g, const char *s) { set_sub(g, "%s", s ? s : ""); }

/* 履历环形保留最近 PL_LOG_LINES 条；排版当场做一次，render 只读。 */
static void push_log(game_handle_t g, const char *line)
{
    if (!line || !line[0]) return;
    if (g->log_count == PL_LOG_LINES) {
        memmove(&g->log[0], &g->log[1], (PL_LOG_LINES - 1) * sizeof(g->log[0]));
        g->log_count--;
    } else {
        g->log_count++;
    }
    log_entry_t *e = &g->log[g->log_count - 1];
    snprintf(e->text, PL_LOG_MAX, "第%d回 %s", g->core.turn, line);
    pl_text_layout(e->text, TEXT_W, &e->lay);
}

/* 存档只搬「这辈子的数字」，履历/文案不入库：重启后从本回合重新开始，
 * 少 3KB 的 NVS 写放大，也省掉一套版本兼容。 */
static void save_run(game_handle_t g)
{
    if (!g->core.poet) return;
    pl_save_data_t d;
    pl_save_capture(&d, &g->core, g->selected, PL_PAGE_HOME);
    (void)pl_store_save(&d);
}

/* 开机若有进行中的生涯，直接回到那一回合的月度面板。履历不入库，所以续档
 * 的正文只报数字，不假装还原叙事；已经死了的档在解码后立刻抹掉。 */
static void try_resume(game_handle_t g)
{
    pl_save_data_t d;
    bool found = false;
    if (pl_store_load(&d, &found) != ESP_OK || !found) return;
    if (d.dead || d.turn < 1 || d.turn > POETS_MAX_TURNS) {
        (void)pl_store_erase();
        return;
    }
    g->selected = d.poet_index;
    poets_game_init(&g->core, g_poets[d.poet_index], d.rng ? d.rng : 1u);
    pl_save_restore(&d, &g->core);
    set_title(g, "%s", poets_game_era_label((uint8_t)(g->core.turn - 1)));
    set_msg(g, "接续上一世：第%d回，银两%d、粮食%d、名声%d。", (int)g->core.turn,
            (int)g->core.silver, (int)g->core.food, (int)g->core.fame);
    g->sub[0] = '\0';
    g->page = PL_PAGE_HOME;
}

/* 只画放得下的整字：宁可少一个字，也不把笔画切一半。 */
static void draw_clipped(const char *s, int x, int y, int max_w, Color color)
{
    int pen = x;
    const char *p = s;
    while (*p) {
        uint32_t cp = 0;
        p += pl_font_utf8_next(p, &cp);
        const int adv = pl_font_advance(cp);
        if (pen - x + adv > max_w) break;
        pen = pl_font_draw_cp(cp, pen, y, color);
    }
}

static void draw_centered(const char *s, int center_x, int y, Color color)
{
    pl_font_draw(s, center_x - pl_font_width(s) / 2, y, color);
}

static void draw_box(const zone_t *z, Color fill, Color border)
{
    DrawRectangle(z->x, z->y, z->w, z->h, fill);
    DrawRectangleLines(z->x, z->y, z->w, z->h, border);
}

/* 一行文字在框内水平垂直居中，超宽则裁掉放不下的整字。 */
static void draw_in_box(const char *s, const zone_t *z, Color color)
{
    draw_clipped(s, z->x + (z->w - pl_font_width(s)) / 2,
                 z->y + (z->h - PL_FONT_CELL) / 2, z->w - 8, color);
}

/* 多行文字在框内居中；行数放不下时从中间往外排，超出框顶的行不画。 */
static void draw_layout_in_box(const char *s, const pl_text_layout_t *lay,
                               const zone_t *z, int pitch, Color color)
{
    const int n = lay->count > 0 ? (lay->count < 3 ? lay->count : 3) : 1;
    int y = z->y + (z->h - n * pitch) / 2;
    if (y < z->y) y = z->y;
    pl_text_draw_lines(s, lay, 0, n, z->x + 8, y, pitch, color);
}

/* ------------------------------------------------------------ game logic */

static int tab_action_index(game_handle_t g, int slot);
static void prepare(game_handle_t g);

static const char *stat_name(int i)
{
    static const char *const names[6] = {"银两", "粮食", "名声", "体魄", "诗兴", "人脉"};
    return names[i];
}

static int16_t stat_value(const poets_game_t *core, int i)
{
    switch (i) {
    case 0: return core->silver;
    case 1: return core->food;
    case 2: return core->fame;
    case 3: return core->health;
    case 4: return core->muse;
    default: return core->friends;
    }
}

/* 小传页还没开局（core.poet 为空），六维得直接读诗人表，否则画出来全是 0。 */
static int16_t poet_stat(const poets_poet_t *p, int i)
{
    switch (i) {
    case 0: return p->stats.silver;
    case 1: return p->stats.food;
    case 2: return p->stats.fame;
    case 3: return p->stats.health;
    case 4: return p->stats.muse;
    default: return p->stats.friends;
    }
}

static Color stat_color(int i)
{
    switch (i) {
    case 0: return COL_SILVER;
    case 1: return COL_FOOD;
    case 2: return COL_FAME;
    case 3: return COL_HEALTH;
    case 4: return COL_MUSE;
    default: return COL_FRIENDS;
    }
}

static void begin_turn(game_handle_t g)
{
    /* 顶栏左边只放年号（含月份，最长 11 字），回合数放右边：两边加起来
     * 才不会互相挤掉字。 */
    set_title(g, "%s", poets_game_era_label((uint8_t)(g->core.turn - 1)));
    set_msg(g, "这个月的日子开始了。挑一件事做：谋生、交际，还是自修。");
    g->sub[0] = '\0';
    g->page = PL_PAGE_HOME;
    save_run(g);
}

static void show_ending(game_handle_t g)
{
    poets_game_ending(&g->core, &g->ending);
    const poets_game_t *c = &g->core;
    char narr[192];
    if (c->dead) {
        set_title(g, "%s", poets_death_reason_name(c->death_reason));
        snprintf(narr, sizeof(narr), "%s",
                 poets_death_reason_text(c->death_reason));
    } else {
        set_title(g, "十五年修满");
        snprintf(narr, sizeof(narr), "%s放下笔，走完了这十五年。", c->poet->name);
    }
    /* 称号/评语来自内容层（poets_ending_*）。诗词数已经在顶栏，得分单独画，
     * 免得把三段数字拼进正文后把死因叙述挤出屏幕。 */
    set_msg_plain(g, poets_ending_subtitle(g->ending.title));
    set_sub_plain(g, narr);
    const int pages = (g->log_count + PL_LOG_PAGE - 1) / PL_LOG_PAGE;
    g->log_page = pages > 0 ? pages - 1 : 0;   /* 打开就看到最后几笔 */
    g->page = PL_PAGE_ENDING;
    /* 一生已定，档就没必要留着了：重启后从选诗人开始，而不是又看到结局。 */
    (void)pl_store_erase();
    pl_sfx_play(c->dead ? PL_TONE_DEATH : PL_TONE_ENDING);
}

static void open_event(game_handle_t g, const char *month_line)
{
    /* pick_event 每回合必发一张事件卡，不会返回 NULL；仍留空串兜底。 */
    g->cur_event = poets_game_pick_event(&g->core);
    g->event_resolved = 0;
    set_title(g, "%s", g->cur_event ? g->cur_event->title : "本月无事");
    set_msg_plain(g, g->cur_event ? g->cur_event->text : "什么也没发生。");
    set_sub_plain(g, month_line);
    g->page = PL_PAGE_EVENT;
    pl_sfx_play(PL_TONE_EVENT);
}

/* 行动结算后的共同路径：月结 -> 死亡判定 -> 事件。 */
static void after_action(game_handle_t g, int action_index)
{
    char line[PL_LOG_MAX];
    line[0] = '\0';
    poets_game_process_month(&g->core, line, sizeof(line));
    push_log(g, line);
    if (g->core.dead) {
        show_ending(g);
        return;
    }
    g->core.last_category = poets_game_action_def(&g->core, action_index)->category;
    open_event(g, line);
}

static void do_action(game_handle_t g, int index, bool correct)
{
    char line[PL_LOG_MAX];
    poets_game_resolve_action(&g->core, index, correct, line, sizeof(line));
    push_log(g, line);
    pl_sfx_play(PL_TONE_ACTION);
    after_action(g, index);
}

static void fill_quiz(game_handle_t g, const poets_question_t *q)
{
    g->question = q;
    g->answered = 0;
    g->answered_correct = 0;
    g->pick_shown = -1;
    for (int i = 0; i < 4; i++) {
        snprintf(g->opt[i], PL_OPT_MAX, "%s. %s", s_option_tags[i],
                 q ? q->options[i] : "");
    }
}

static void open_quiz(game_handle_t g, int category, int pending, int payload)
{
    const poets_question_t *q = poets_game_pick_question(&g->core, category);
    if (!q) {                       /* 空题池：当作答对直接结算，不能卡死流程 */
        g->question = NULL;
        if (pending == PEND_ACTION) do_action(g, payload, true);
        return;
    }
    g->quiz_category = category;
    g->pending = (uint8_t)pending;
    g->pending_action = payload;
    fill_quiz(g, q);
    set_title(g, "答题·%s", (category >= 0 && category < POETS_POOL_COUNT)
                                ? g_pools[category].name : "杂学");
    set_msg_plain(g, q->q);
    g->sub[0] = '\0';
    g->page = PL_PAGE_QUIZ;
}

static void tap_action(game_handle_t g, int index)
{
    if (g->core.dead || (g->cur_event != NULL && !g->event_resolved)) return;
    if (!poets_game_action_enabled(&g->core, index)) {
        set_sub(g, "此路不通：银两、粮食、名声、人脉、诗兴不够，或还没有官职。");
        pl_sfx_play(PL_TONE_REFUSE);
        return;
    }
    const int quiz = poets_game_action_quiz(&g->core, index);
    if (quiz == POETS_CAT_NONE) {
        do_action(g, index, true);
        return;
    }
    open_quiz(g, quiz, PEND_ACTION, index);
}

static void finish_quiz(game_handle_t g)
{
    const bool correct = g->answered_correct != 0;
    const int pending = g->pending;
    const int action = g->pending_action;
    const poets_choice_t *choice = g->pending_choice;
    g->pending = PEND_NONE;
    g->pending_choice = NULL;
    g->question = NULL;
    if (pending == PEND_ACTION) {
        do_action(g, action, correct);
        return;
    }
    if (pending == PEND_EVENT && choice != NULL) {
        char result[PL_LOG_MAX];
        poets_game_apply_choice(&g->core, choice, correct, result, sizeof(result));
        push_log(g, result);
        set_msg_plain(g, result);
        g->sub[0] = '\0';
        g->event_resolved = 1;
        g->page = PL_PAGE_EVENT;
        if (g->core.dead) show_ending(g);
    }
}

static void tap_event_choice(game_handle_t g, int index)
{
    if (g->cur_event == NULL || g->event_resolved || g->core.dead) return;
    if (index >= g->cur_event->choice_count) return;
    const poets_choice_t *choice = g->cur_event->choices[index];
    if (choice == NULL) return;
    if (choice->quiz_category != POETS_CAT_NONE) {
        g->pending_choice = choice;
        open_quiz(g, choice->quiz_category, PEND_EVENT, 0);
        return;
    }
    char result[PL_LOG_MAX];
    poets_game_apply_choice(&g->core, choice, false, result, sizeof(result));
    push_log(g, result);
    set_msg_plain(g, result);
    g->sub[0] = '\0';
    g->event_resolved = 1;
    if (g->core.dead) show_ending(g);
}

static void next_turn(game_handle_t g)
{
    if (g->cur_event != NULL && !g->event_resolved) return;
    g->cur_event = NULL;
    if (!poets_game_advance_turn(&g->core)) {
        show_ending(g);
        return;
    }
    begin_turn(g);
}

static void open_bio(game_handle_t g)
{
    const poets_poet_t *p = g_poets[g->selected];
    set_title(g, "%s · %s", p->name, p->era);
    set_msg_plain(g, p->story);
    set_sub_plain(g, p->desc);
    g->page = PL_PAGE_BIO;
}

static void start_run(game_handle_t g)
{
    /* 固定种子：同一位诗人每次开局走同一条随机流，replay 才能断言数值。 */
    const uint32_t seed = 0x5EEDCAFEu ^ (uint32_t)(g->selected + 1) * 2654435761u;
    poets_game_init(&g->core, g_poets[g->selected], seed);
    g->log_count = 0;
    g->log_page = 0;
    g->ending = (poets_ending_t){0};
    g->cur_event = NULL;
    (void)poets_game_advance_turn(&g->core);      /* turn 0 -> 1 */
    begin_turn(g);
}

static void handle_tap(game_handle_t g, int zone)
{
    /* 先给一声「点到了」，同一帧里后面的语义音会把它替换掉（player 只留最新一条），
     * 所以既不会静默也不会叠成两声。 */
    if (zone != Z_NONE) pl_sfx_play(PL_TONE_TAP);
    switch (g->page) {
    case PL_PAGE_TITLE:
        if (zone >= Z_POET0 && zone < Z_POET0 + POETS_POET_COUNT) {
            g->selected = zone - Z_POET0;
        } else if (zone == Z_START && g->selected >= 0) {
            open_bio(g);
        }
        break;
    case PL_PAGE_BIO:
        if (zone == Z_BIO_BACK) {
            set_title(g, "诗与远方");
            g->msg[0] = '\0';
            g->sub[0] = '\0';
            g->page = PL_PAGE_TITLE;
        } else if (zone == Z_BIO_BEGIN) {
            start_run(g);
        }
        break;
    case PL_PAGE_HOME:
        if (zone == Z_HOME_ACT) {
            g->sub[0] = '\0';
            g->page = PL_PAGE_ACTION;
        } else if (zone == Z_HOME_END) {
            show_ending(g);
        }
        break;
    case PL_PAGE_ACTION:
        if (zone == Z_BACK) {
            g->sub[0] = '\0';
            g->page = PL_PAGE_HOME;
            break;
        }
        if (zone >= Z_TAB0 && zone < Z_TAB0 + 3) {
            g->tab = (uint8_t)(zone - Z_TAB0);
            g->sub[0] = '\0';
            break;
        }
        if (zone >= Z_ACT0 && zone < Z_ACT0 + PL_TAB_ROWS) {
            const int index = tab_action_index(g, zone - Z_ACT0);
            if (index >= 0) tap_action(g, index);
        }
        break;
    case PL_PAGE_QUIZ:
        /* 答完就是“看解释”，屏幕上没有任何可选东西了：整屏都是「继续」。
         * 480px 上要点 56px 高的按钮纯属折磨，replay 也因此不必猜按钮位置。 */
        if (g->answered) {
            finish_quiz(g);
            break;
        }
        if (zone >= Z_OPT0 && zone < Z_OPT0 + 4 && g->question) {
            const int pick = zone - Z_OPT0;
            g->answered = 1;
            g->pick_shown = pick;
            g->answered_correct = pick == g->question->answer ? 1 : 0;
            set_sub(g, "%s%s", g->question->explain,
                    g->question->recite ? g->question->recite : "");
            pl_sfx_play(g->answered_correct ? PL_TONE_QUIZ_OK : PL_TONE_QUIZ_BAD);
        }
        break;
    case PL_PAGE_EVENT:
        if (g->event_resolved || g->core.dead) {
            next_turn(g);
            break;
        }
        if (zone == Z_CH0) tap_event_choice(g, 0);
        else if (zone == Z_CH1) tap_event_choice(g, 1);
        break;
    case PL_PAGE_ENDING: {
        const int pages = (g->log_count + PL_LOG_PAGE - 1) / PL_LOG_PAGE;
        if (zone == Z_LOG_PREV && g->log_page > 0) g->log_page--;
        else if (zone == Z_LOG_NEXT && g->log_page + 1 < pages) g->log_page++;
        else if (zone == Z_RESTART) {
            game_reset(g);
            prepare(g);
        }
        break;
    }
    default:
        break;
    }
}

/* ------------------------------------------- 几何：只在 update 路径上算 */

/* 专属技能排在表尾（index >= POETS_ACTION_COUNT），归进「自修」页签。 */
static int tab_of_action(int i)
{
    return i < POETS_ACTION_COUNT ? s_tab_of_action[i] : 2;
}

static int tab_action_index(game_handle_t g, int slot)
{
    int seen = 0;
    const int count = poets_game_action_count(&g->core);
    for (int i = 0; i < count; i++) {
        if (tab_of_action(i) != g->tab) continue;
        if (seen == slot) return i;
        seen++;
    }
    return -1;
}

static int tab_action_total(game_handle_t g)
{
    int seen = 0;
    const int count = poets_game_action_count(&g->core);
    for (int i = 0; i < count; i++) {
        if (tab_of_action(i) == g->tab) seen++;
    }
    return seen;
}

static void build_title_zones(game_handle_t g)
{
    for (int i = 0; i < POETS_POET_COUNT; i++) {
        zone_add(g, Z_POET0 + i, PAD, 72 + i * ROW_H, TEXT_W, ROW_H - 4);
    }
    zone_add(g, Z_START, PAD, BUTTON_TOP, TEXT_W, BUTTON_H);
}

static void build_pair_zones(game_handle_t g, int left_id, int right_id)
{
    const int half = (TEXT_W - 16) / 2;
    zone_add(g, left_id, PAD, BUTTON_TOP, half, BUTTON_H);
    zone_add(g, right_id, PAD + half + 16, BUTTON_TOP, half, BUTTON_H);
}

static void build_home_zones(game_handle_t g)
{
    zone_add(g, Z_HOME_ACT, PAD, BUTTON_TOP, 340, BUTTON_H);
    zone_add(g, Z_HOME_END, PAD + 348, BUTTON_TOP, 100, BUTTON_H);
}

static void build_action_zones(game_handle_t g)
{
    const int tab_w = (TEXT_W - 2 * 8) / 3;
    for (int t = 0; t < 3; t++) {
        zone_add(g, Z_TAB0 + t, PAD + t * (tab_w + 8), 46, tab_w, TAB_H);
    }
    int rows = tab_action_total(g);
    if (rows > PL_TAB_ROWS) rows = PL_TAB_ROWS;
    for (int r = 0; r < rows; r++) {
        zone_add(g, Z_ACT0 + r, PAD, 84 + r * ROW_H, TEXT_W, ROW_H - 2);
    }
    zone_add(g, Z_BACK, PAD, BUTTON_TOP, TEXT_W, BUTTON_H);
}

static void build_quiz_zones(game_handle_t g)
{
    for (int i = 0; i < 4; i++) {
        zone_add(g, Z_OPT0 + i, PAD, 108 + i * 52, TEXT_W, 48);
    }
    if (g->answered) zone_add(g, Z_QUIZ_GO, PAD, BUTTON_TOP, TEXT_W, BUTTON_H);
}

static void build_event_zones(game_handle_t g)
{
    if (g->event_resolved || g->core.dead) {
        zone_add(g, Z_EVENT_NEXT, PAD, BUTTON_TOP, TEXT_W, BUTTON_H);
        return;
    }
    zone_add(g, Z_CH0, PAD, 288, TEXT_W, 44);
    zone_add(g, Z_CH1, PAD, 340, TEXT_W, 44);
}

static void build_ending_zones(game_handle_t g)
{
    zone_add(g, Z_LOG_PREV, PAD, 368, 88, 26);
    zone_add(g, Z_LOG_NEXT, GAME_WIDTH - PAD - 88, 368, 88, 26);
    zone_add(g, Z_RESTART, PAD, BUTTON_TOP, TEXT_W, BUTTON_H);
}

/* 头像左右小幅呼吸：24 帧（0.8 秒）一个来回的三角波，幅度 ±3px，每位诗人错开
 * 相位，免得六张脸齐步走。偏移必须在 update 路径算好，render 只读。 */
static int avatar_sway(uint32_t tick, int poet_index)
{
    const uint32_t t = (tick + (uint32_t)poet_index * 5u) % 24u;
    const uint32_t tri = t < 12u ? t : 24u - t;   /* 0..12 */
    return (int)(tri / 2u) - 3;                   /* -3..+3 */
}

static void prepare(game_handle_t g)
{
    for (int i = 0; i < POETS_POET_COUNT; i++) {
        g->avatar_dx[i] = (int8_t)avatar_sway((uint32_t)g->state.tick, i);
    }
    /* 结局页右上角有朱砂印章，正文得给它让出 64px，否则第二行会从字底下穿过去。 */
    const int body_w = g->page == PL_PAGE_ENDING ? TEXT_W - 64 : TEXT_W;
    pl_text_layout(g->msg, body_w, &g->lay_msg);
    pl_text_layout(g->sub, body_w, &g->lay_sub);
    for (int i = 0; i < 4; i++) {
        pl_text_layout(g->opt[i], TEXT_W - 16, &g->lay_opt[i]);
    }
    g->zone_count = 0;
    switch (g->page) {
    case PL_PAGE_TITLE: build_title_zones(g); break;
    case PL_PAGE_BIO: build_pair_zones(g, Z_BIO_BACK, Z_BIO_BEGIN); break;
    case PL_PAGE_HOME: build_home_zones(g); break;
    case PL_PAGE_ACTION: build_action_zones(g); break;
    case PL_PAGE_QUIZ: build_quiz_zones(g); break;
    case PL_PAGE_EVENT: build_event_zones(g); break;
    case PL_PAGE_ENDING: build_ending_zones(g); break;
    default: break;
    }
}

/* ------------------------------------------------------------- rendering */

static void render_stats(game_handle_t g, int top)
{
    const poets_poet_t *preview = (g->core.poet || g->selected < 0) ? NULL
                                                                   : g_poets[g->selected];
    for (int i = 0; i < 6; i++) {
        const int x = PAD + (i % 2) * 232;
        const int y = top + (i / 2) * TIGHT_PITCH;
        const int16_t v = preview ? poet_stat(preview, i) : stat_value(&g->core, i);
        pl_font_draw(stat_name(i), x, y, COL_DIM);
        const int bar_x = x + 56, bar_w = 100;
        DrawRectangle(bar_x, y + 6, bar_w, 12, COL_PANEL);
        int fill = (v * bar_w) / 100;
        if (fill < 0) fill = 0;
        if (fill > bar_w) fill = bar_w;
        DrawRectangle(bar_x, y + 6, fill, 12, stat_color(i));
        char num[12];
        snprintf(num, sizeof(num), "%d", (int)v);
        pl_font_draw(num, bar_x + bar_w + 8, y, COL_TEXT);
    }
}

static void render_header(game_handle_t g)
{
    DrawRectangle(0, 0, GAME_WIDTH, HEADER_H, COL_PANEL);
    /* 选定诗人之后，每一页的页眉右侧都是这位诗人的脸；回合信息往左让位。 */
    const bool face = g->selected >= 0 && g->selected < POETS_POET_COUNT;
    const int face_x = GAME_WIDTH - PL_AVATAR_W - 4;
    draw_clipped(g->title, PAD, 8, TEXT_W - 128 - (face ? PL_AVATAR_W + 8 : 0), COL_TEXT);
    if (face) pl_avatar_draw(g->selected, face_x, 0, g->avatar_dx[g->selected]);
    if (!g->core.poet) return;
    char right[PL_TITLE_MAX];
    snprintf(right, sizeof(right), "第%d回 %d首", g->core.turn, (int)g->core.poems);
    const int right_edge = face ? face_x - 8 : GAME_WIDTH - PAD;
    pl_font_draw(right, right_edge - pl_font_width(right), 8, COL_DIM);
}

/* 正文 + 次级文本，从上往下顺序排布；画到 bottom 就停，绝不压下一块版面。
 * bottom 由调用方给：事件页在选项出现时是选项顶边，其余页是主按钮顶边。 */
static void render_body(game_handle_t g, int top, int max_msg_lines, int bottom)
{
    const int drawn = pl_text_draw_lines(g->msg, &g->lay_msg, 0, max_msg_lines,
                                         PAD, top, LINE_PITCH, COL_TEXT);
    const int y = top + drawn * LINE_PITCH + 4;
    if (g->sub[0] && y + TIGHT_PITCH <= bottom) {
        pl_text_draw_lines(g->sub, &g->lay_sub, 0, (bottom - y) / TIGHT_PITCH, PAD, y,
                           TIGHT_PITCH, COL_DIM);
    }
}

static void render_title(game_handle_t g)
{
    draw_centered("诗与远方", GAME_WIDTH / 2, 8, COL_SILVER);
    draw_centered("古代诗人生存手册", GAME_WIDTH / 2, 40, COL_DIM);
    for (int i = 0; i < POETS_POET_COUNT; i++) {
        const zone_t *z = zone_find(g, Z_POET0 + i);
        const poets_poet_t *p = g_poets[i];
        if (!z) continue;
        const bool sel = i == g->selected;
        draw_box(z, sel ? COL_SEL : COL_PANEL, sel ? COL_SILVER : COL_LINE);
        pl_avatar_draw(i, z->x + 4, z->y + 2, g->avatar_dx[i]);
        const int cy = z->y + (z->h - PL_FONT_CELL) / 2;
        const int name_at = z->x + PL_AVATAR_W + 12;
        pl_font_draw(p->name, name_at, cy, sel ? COL_SILVER : COL_TEXT);
        const int used = (name_at - z->x) + pl_font_width(p->name) + 20;
        draw_clipped(p->tagline, z->x + used, cy, z->w - used - 8,
                     sel ? COL_TEXT : COL_DIM);
    }
    const zone_t *start = zone_find(g, Z_START);
    if (start) {
        draw_box(start, g->selected >= 0 ? COL_SEL : COL_PANEL, COL_SILVER);
        draw_in_box("入 生", start, COL_TEXT);
    }
    draw_centered(g->selected >= 0 ? "十五回合，活成一位诗人" : "先选一位诗人",
                  GAME_WIDTH / 2, 376, COL_DIM);
}

static void render_bio(game_handle_t g)
{
    render_header(g);
    render_body(g, CONTENT_TOP - 52, 6, 292);   /* 292 = 六维条顶边 */
    render_stats(g, 296);
    const zone_t *back = zone_find(g, Z_BIO_BACK);
    const zone_t *begin = zone_find(g, Z_BIO_BEGIN);
    if (back) { draw_box(back, COL_PANEL, COL_LINE); draw_in_box("换人", back, COL_DIM); }
    if (begin) { draw_box(begin, COL_SEL, COL_SILVER); draw_in_box("开始生涯", begin, COL_TEXT); }
}

static void render_home(game_handle_t g)
{
    render_header(g);
    render_stats(g, STAT_TOP);
    render_body(g, CONTENT_TOP, 6, BUTTON_TOP);
    const zone_t *act = zone_find(g, Z_HOME_ACT);
    if (act) { draw_box(act, COL_SEL, COL_SILVER); draw_in_box("本月行动", act, COL_TEXT); }
    const zone_t *end = zone_find(g, Z_HOME_END);
    if (end) { draw_box(end, COL_PANEL, COL_LINE); draw_in_box("看结局", end, COL_DIM); }
}

static void render_action(game_handle_t g)
{
    render_header(g);
    for (int t = 0; t < 3; t++) {
        const zone_t *z = zone_find(g, Z_TAB0 + t);
        if (!z) continue;
        const bool on = t == g->tab;
        draw_box(z, on ? COL_SEL : COL_PANEL, on ? COL_SILVER : COL_LINE);
        draw_in_box(s_tab_names[t], z, on ? COL_TEXT : COL_DIM);
    }
    for (int slot = 0; slot < PL_TAB_ROWS; slot++) {
        const zone_t *z = zone_find(g, Z_ACT0 + slot);
        const int index = tab_action_index(g, slot);
        if (!z || index < 0) continue;
        const poets_action_def_t *def = poets_game_action_def(&g->core, index);
        const char *label = def->label;      /* 立刻取用：专属项共用一份静态缓冲 */
        const char *desc = def->desc;
        const bool usable = poets_game_action_enabled(&g->core, index);
        draw_box(z, usable ? COL_PANEL : COL_BG, usable ? COL_LINE : COL_PANEL);
        draw_clipped(label, z->x + 8, z->y + 2, 200, usable ? COL_TEXT : COL_DIM);
        draw_clipped(desc, z->x + 8, z->y + 24, z->w - 24, usable ? COL_DIM : COL_PANEL);
        if (!usable) {
            draw_clipped("不可", z->x + z->w - 8 - pl_font_width("不可"), z->y + 2, 60,
                         COL_BAD);
        }
    }
    const zone_t *back = zone_find(g, Z_BACK);
    if (back) { draw_box(back, COL_PANEL, COL_LINE); draw_in_box("返回", back, COL_DIM); }
    if (g->sub[0]) pl_text_draw_lines(g->sub, &g->lay_sub, 0, 1, PAD, 372, TIGHT_PITCH, COL_BAD);
}

static void render_quiz(game_handle_t g)
{
    render_header(g);
    pl_text_draw_lines(g->msg, &g->lay_msg, 0, 2, PAD, 48, LINE_PITCH, COL_TEXT);
    for (int i = 0; i < 4; i++) {
        const zone_t *z = zone_find(g, Z_OPT0 + i);
        if (!z) continue;
        Color border = COL_LINE, text = COL_TEXT;
        if (g->answered && g->question) {
            if (i == g->question->answer) { border = COL_OK; text = COL_OK; }
            else if (i == g->pick_shown) { border = COL_BAD; text = COL_BAD; }
        }
        draw_box(z, COL_PANEL, border);
        draw_layout_in_box(g->opt[i], &g->lay_opt[i], z, TIGHT_PITCH, text);
    }
    if (g->answered) {
        pl_text_draw_lines(g->sub, &g->lay_sub, 0, 3, PAD, 320, TIGHT_PITCH,
                           g->answered_correct ? COL_OK : COL_MUSE);
        const zone_t *go = zone_find(g, Z_QUIZ_GO);
        if (go) { draw_box(go, COL_SEL, COL_SILVER); draw_in_box("继续", go, COL_TEXT); }
    } else {
        draw_centered("选一个答案", GAME_WIDTH / 2, 368, COL_DIM);
    }
}

static void render_event(game_handle_t g)
{
    render_header(g);
    /* 事件页不画六维，正文直接从顶栏下方起排；288 = 第一个选项的顶边，
     * 结算后那里换成 400 的主按钮，正文才允许继续往下走。 */
    render_body(g, CONTENT_TOP - 52, 5,
                (g->event_resolved || g->core.dead) ? BUTTON_TOP : 280);
    if (g->event_resolved || g->core.dead) {
        const zone_t *next = zone_find(g, Z_EVENT_NEXT);
        if (next) {
            draw_box(next, COL_SEL, COL_SILVER);
            draw_in_box(g->core.turn >= POETS_MAX_TURNS ? "最后盘点" : "下一回合",
                        next, COL_TEXT);
        }
        return;
    }
    for (int i = 0; i < 2; i++) {
        const zone_t *z = zone_find(g, Z_CH0 + i);
        const poets_choice_t *c = (g->cur_event && i < g->cur_event->choice_count)
                                     ? g->cur_event->choices[i] : NULL;
        if (!z || !c) continue;
        draw_box(z, COL_PANEL, COL_LINE);
        draw_clipped(c->label, z->x + 8, z->y + (z->h - PL_FONT_CELL) / 2, z->w - 80,
                    COL_TEXT);
        if (c->quiz_category != POETS_CAT_NONE) {
            draw_clipped("答题", z->x + z->w - 8 - pl_font_width("答题"),
                         z->y + (z->h - PL_FONT_CELL) / 2, 60, COL_MUSE);
        }
    }
}

static void render_ending(game_handle_t g)
{
    const int pages = (g->log_count + PL_LOG_PAGE - 1) / PL_LOG_PAGE;
    render_header(g);
    /* 印章：右上角一块朱砂，里面一个字，比任何字体特效都像落款。 */
    const int seal_x = GAME_WIDTH - PAD - 56, seal_y = 44;
    draw_clipped(poets_ending_title(g->ending.title), PAD, 44, 200, COL_SILVER);
    char score[32];
    snprintf(score, sizeof(score), "得分%ld", (long)g->ending.score);
    draw_clipped(score, 392 - pl_font_width(score), 44, 180, COL_TEXT);
    DrawRectangle(seal_x, seal_y, 56, 56, COL_SEAL);
    draw_centered(poets_ending_seal(g->ending.title), seal_x + 28, seal_y + 16, COL_TEXT);
    pl_text_draw_lines(g->msg, &g->lay_msg, 0, 2, PAD, 72, LINE_PITCH, COL_TEXT);
    /* 死因叙述实测最长 4 行，画 3 行会把末句拦腰砍断。履历从 234 起，正好留出身位。 */
    pl_text_draw_lines(g->sub, &g->lay_sub, 0, 4, PAD, CONTENT_TOP, TIGHT_PITCH, COL_DIM);

    const int first = g->log_page * PL_LOG_PAGE;
    for (int r = 0; r < PL_LOG_PAGE; r++) {
        const int k = first + r;
        if (k >= g->log_count) break;
        const log_entry_t *e = &g->log[k];
        pl_text_draw_lines(e->text, &e->lay, 0, 2, PAD, 234 + r * 56, TIGHT_PITCH, COL_TEXT);
    }
    if (g->log_count == 0) {
        draw_centered("这一世还没来得及留下履历", GAME_WIDTH / 2, 236, COL_DIM);
    } else {
        char pager[24];
        snprintf(pager, sizeof(pager), "%d/%d", g->log_page + 1, pages);
        draw_centered(pager, GAME_WIDTH / 2, 368, COL_DIM);
        const zone_t *prev = zone_find(g, Z_LOG_PREV);
        const zone_t *nextz = zone_find(g, Z_LOG_NEXT);
        if (prev) { draw_box(prev, COL_PANEL, COL_LINE); draw_in_box("上页", prev, COL_DIM); }
        if (nextz) { draw_box(nextz, COL_PANEL, COL_LINE); draw_in_box("下页", nextz, COL_DIM); }
    }
    const zone_t *again = zone_find(g, Z_RESTART);
    if (again) { draw_box(again, COL_SEL, COL_SILVER); draw_in_box("再来一局", again, COL_TEXT); }
}

/* -------------------------------------------------------------- surface */

bool game_create(const game_config_t *config, game_handle_t *ret_handle)
{
    if (!ret_handle) return false;
    *ret_handle = NULL;
    if (!config || !config->width || !config->height) return false;
    game_handle_t game = calloc(1, sizeof(*game));
    if (!game) return false;
    game->config = *config;
    game->selected = -1;
    game->pick_shown = -1;
    game->core.poet = NULL;
    set_title(game, "诗与远方");
    (void)pl_store_init();
    try_resume(game);
    prepare(game);
    *ret_handle = game;
    return true;
}

void game_delete(game_handle_t handle)
{
    free(handle);
}

void game_reset(game_handle_t handle)
{
    if (!handle) return;
    const game_config_t config = handle->config;
    memset(handle, 0, sizeof(*handle));
    handle->config = config;
    handle->page = PL_PAGE_TITLE;
    handle->selected = -1;
    handle->pick_shown = -1;
    set_title(handle, "诗与远方");
}

void game_set_paused(game_handle_t game, bool paused)
{
    if (game) game->state.paused = paused;
}

void game_set_pointer(game_handle_t game, int32_t x, int32_t y, bool pressed)
{
    if (!game) return;
    game->state.pointer_x = x < 0 ? 0 : (x >= game->config.width ? game->config.width - 1 : x);
    game->state.pointer_y = y < 0 ? 0 : (y >= game->config.height ? game->config.height - 1 : y);
    game->state.pointer_down = pressed;
    if (pressed && !game->prev_pressed) {
        game->tap_x = game->state.pointer_x;
        game->tap_y = game->state.pointer_y;
        game->tap_pending = true;
    }
    game->prev_pressed = pressed;
}

void game_update(game_handle_t game)
{
    if (!game || game->state.paused) return;
    ++game->state.tick;
    prepare(game);                       /* 先有几何，才能判定命中 */
    if (game->tap_pending) {
        game->tap_pending = false;
        handle_tap(game, zone_hit(game, game->tap_x, game->tap_y));
        prepare(game);                   /* 换页后立刻让 render 看到新页的几何 */
    }
    game_snapshot_t *s = &game->state;
    const poets_game_t *c = &game->core;
    s->page = game->page;
    s->tab = game->tab;
    s->turn = c->poet ? c->turn : 0;
    s->silver = c->silver;
    s->food = c->food;
    s->fame = c->fame;
    s->health = c->health;
    s->muse = c->muse;
    s->friends = c->friends;
    s->poems = c->poems;
    s->dead = c->dead;
    s->event_pending = game->cur_event != NULL && !game->event_resolved;
}

bool game_render(game_handle_t game)
{
    if (!game) return false;
    BeginDrawing();
    if (!MosaicoFastFrameAvailable()) {
        EndDrawing();
        return false;
    }
    ClearBackground(COL_BG);
    switch (game->page) {
    case PL_PAGE_TITLE: render_title(game); break;
    case PL_PAGE_BIO: render_bio(game); break;
    case PL_PAGE_HOME: render_home(game); break;
    case PL_PAGE_ACTION: render_action(game); break;
    case PL_PAGE_QUIZ: render_quiz(game); break;
    case PL_PAGE_EVENT: render_event(game); break;
    case PL_PAGE_ENDING: render_ending(game); break;
    default: break;
    }
    EndDrawing();
    return true;
}

game_snapshot_t game_read(game_handle_t game)
{
    if (!game) return (game_snapshot_t){0};
    return game->state;
}
