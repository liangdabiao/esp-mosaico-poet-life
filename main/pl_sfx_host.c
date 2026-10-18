// SPDX-License-Identifier: Apache-2.0
// 宿主模拟器音效桩：没有音频设备，每次调用都是空操作。
// 只出现在 game.sim.json，设备构建用 pl_sfx.c。
#include "pl_sfx.h"

bool pl_sfx_start(void)
{
    return true; /* 报告「静默就绪」，让 game.c 两侧走同一条路径。 */
}

void pl_sfx_play(pl_tone_t tone)
{
    (void)tone;
}
