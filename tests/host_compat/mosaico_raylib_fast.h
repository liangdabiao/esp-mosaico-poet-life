// SPDX-License-Identifier: Apache-2.0
// 宿主测试用的最小引擎替身：只声明字模 blit 实际用到的类型与函数，
// 让 pl_font/pl_text 能脱离 raylib 单独编译验证（真机与模拟器用真头文件）。
#pragma once

typedef struct { unsigned char r, g, b, a; } Color;
typedef struct { int x, y, width, height; } Rectangle;

void DrawRectangle(int x, int y, int width, int height, Color color);
int GetScreenWidth(void);
int GetScreenHeight(void);
