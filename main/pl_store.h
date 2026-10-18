// SPDX-License-Identifier: Apache-2.0
// 生涯存档落盘：设备端 NVS blob，宿主端进程静态内存。
// 字节格式的校验/版本兼容全在纯逻辑层 pl_save.c，这里只搬运。
#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "pl_save.h"

/* 打开存储。失败时游戏照常运行，只是不持久化。 */
esp_err_t pl_store_init(void);

/* 读存档。*found=false 表示分区里没有可用记录（首次开机、或存档版本/校验不符）。 */
esp_err_t pl_store_load(pl_save_data_t *data, bool *found);

/* 写存档（立即 commit）。 */
esp_err_t pl_store_save(const pl_save_data_t *data);

/* 擦掉本应用的存档。 */
esp_err_t pl_store_erase(void);
