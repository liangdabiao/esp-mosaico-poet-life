// SPDX-License-Identifier: Apache-2.0
// 设备端音效：RTTTL 旋律经 rtttl_player 在 ES8311 上合成，不需要音频素材文件。
#include "pl_sfx.h"

#include "esp_err.h"

#include "rtttl_player.h"

/* name:defaults(d=音长,o=八度,b=速度):notes */
static const char *const TONES[PL_TONE_COUNT] = {
    [PL_TONE_TAP] = "tp:d=16,o=6,b=600:c6",
    [PL_TONE_ACTION] = "ac:d=8,o=5,b=400:e,g",
    [PL_TONE_REFUSE] = "rf:d=8,o=4,b=300:e,d",
    [PL_TONE_QUIZ_OK] = "wo:d=8,o=5,b=300:c,e,g",
    [PL_TONE_QUIZ_BAD] = "wb:d=8,o=4,b=250:g,e",
    [PL_TONE_EVENT] = "ev:d=8,o=5,b=280:g,c6",
    [PL_TONE_DEATH] = "dt:d=8,o=3,b=200:c,d,e,c",
    [PL_TONE_ENDING] = "en:d=8,o=5,b=250:c,e,g,c6,e6",
};

bool pl_sfx_start(void)
{
    return rtttl_player_start() == ESP_OK;
}

void pl_sfx_play(pl_tone_t tone)
{
    if (tone >= PL_TONE_COUNT) return;
    (void)rtttl_player_play(TONES[tone]);
}
