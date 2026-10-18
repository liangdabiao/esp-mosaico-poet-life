// SPDX-License-Identifier: Apache-2.0
// 音效门面：设备端用 RTTTL 合成（pl_sfx.c + rtttl_player.c），宿主端是同名
// 空实现（pl_sfx_host.c），所以 game.c 两侧完全一致。
#pragma once

#include <stdbool.h>

typedef enum {
    PL_TONE_TAP = 0,     /* 翻页/选中 */
    PL_TONE_ACTION,      /* 行动结算 */
    PL_TONE_REFUSE,      /* 点不动（未解锁/未选） */
    PL_TONE_QUIZ_OK,     /* 答对 */
    PL_TONE_QUIZ_BAD,    /* 答错 */
    PL_TONE_EVENT,       /* 本月事件卡 */
    PL_TONE_DEATH,       /* 六维见底 */
    PL_TONE_ENDING,      /* 修满十五年 */
    PL_TONE_COUNT
} pl_tone_t;

/* 启动音频 worker。返回 false 表示编解码器不可用，游戏随后静默运行。 */
bool pl_sfx_start(void);

/* 投递一次性的短音效，替换队列里更旧的一条；不会阻塞在编解码器上。 */
void pl_sfx_play(pl_tone_t tone);
