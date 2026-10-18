// SPDX-License-Identifier: Apache-2.0
// 设备端 NVS 存档。见 pl_store.h。
#include "pl_store.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "pl_store";
static const char *NAMESPACE = "poet_life";
static const char *KEY_RUN = "run";

esp_err_t pl_store_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs rebuild (%s); erasing and retrying",
                 esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err != ESP_OK) return err;
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t pl_store_load(pl_save_data_t *data, bool *found)
{
    if (!data) return ESP_ERR_INVALID_ARG;
    if (found) *found = false;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return err;
    uint8_t blob[PL_SAVE_BYTES];
    size_t size = sizeof(blob);
    err = nvs_get_blob(handle, KEY_RUN, blob, &size);
    nvs_close(handle);
    if (err != ESP_OK) return err;
    if (!pl_save_decode(blob, size, data)) {
        ESP_LOGW(TAG, "saved run unusable (%u bytes); starting fresh", (unsigned)size);
        return ESP_OK;
    }
    if (found) *found = true;
    return ESP_OK;
}

esp_err_t pl_store_save(const pl_save_data_t *data)
{
    if (!data) return ESP_ERR_INVALID_ARG;
    uint8_t blob[PL_SAVE_BYTES];
    if (pl_save_encode(data, blob, sizeof(blob)) != sizeof(blob)) {
        return ESP_ERR_INVALID_SIZE;
    }
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(handle, KEY_RUN, blob, sizeof(blob));
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) ESP_LOGW(TAG, "run save failed: %s", esp_err_to_name(err));
    return err;
}

esp_err_t pl_store_erase(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;
    err = nvs_erase_key(handle, KEY_RUN);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}
