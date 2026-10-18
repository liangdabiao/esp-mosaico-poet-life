// SPDX-License-Identifier: Apache-2.0
// 诗人头像：40x40 调色板位图，按水平游程绘制（与字模同一条快路径）。
#pragma once

#include "mosaico_raylib_fast.h"
#include "generated/pl_avatar_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 以 (x,y) 为头像左上角绘制第 poet_index 位诗人；dx 是水平摆动偏移，
 * 由 update 路径算好后传进来，这里只读。poet_index 越界时静默不画。 */
void pl_avatar_draw(int poet_index, int x, int y, int dx);

#ifdef __cplusplus
}
#endif
