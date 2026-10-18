// SPDX-License-Identifier: Apache-2.0
// 宿主模拟器存档桩：走一遍和真机完全相同的 encode/decode 字节路径，只是把
// blob 存在进程静态内存里。只出现在 game.sim.json，不进设备 CMakeLists。
#include "pl_store.h"

#include <string.h>

/* 引擎的宿主 esp_err.h 只带常用几个码；取真 IDF 的同一个数值，两侧语义一致。 */
#ifndef ESP_ERR_INVALID_SIZE
#define ESP_ERR_INVALID_SIZE 0x10F
#endif

static uint8_t s_blob[PL_SAVE_BYTES];
static size_t s_size;

esp_err_t pl_store_init(void)
{
    return ESP_OK;
}

esp_err_t pl_store_load(pl_save_data_t *data, bool *found)
{
    if (!data) return ESP_ERR_INVALID_ARG;
    if (found) *found = false;
    if (s_size == 0) return ESP_OK;
    if (!pl_save_decode(s_blob, s_size, data)) {
        s_size = 0;
        return ESP_OK;
    }
    if (found) *found = true;
    return ESP_OK;
}

esp_err_t pl_store_save(const pl_save_data_t *data)
{
    if (!data) return ESP_ERR_INVALID_ARG;
    const size_t n = pl_save_encode(data, s_blob, sizeof(s_blob));
    if (n == 0) return ESP_ERR_INVALID_SIZE;
    s_size = n;
    return ESP_OK;
}

esp_err_t pl_store_erase(void)
{
    s_size = 0;
    memset(s_blob, 0, sizeof(s_blob));
    return ESP_OK;
}
