// SPDX-License-Identifier: MIT
// Lightweight, asynchronous RTTTL playback for short UI/game effects.
// Adapted from the weiqi-quest reference (same upstream project); the playback
// backend targets ESP-Mosaico's ES8311 codec (bsp_audio_codec_speaker_init +
// esp_codec_dev). All playback runs in the player's own task; callers (including
// touch callbacks) only enqueue.
#pragma once

#include <stdbool.h>

#include "esp_err.h"

esp_err_t rtttl_player_start(void);
/* Cancels playback; the worker stays idle so codec locks are never abandoned. */
void rtttl_player_stop(void);
/* Starts immediately and replaces an older queued/playing effect. */
esp_err_t rtttl_player_play(const char *song);
bool rtttl_player_is_playing(void);
