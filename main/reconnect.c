/*
New reconnect logic:
 - Only at startup
 - Only with the last known device
 - Only if the disconnection was due to own poweroff or timeout.
*/
#include "reconnect.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_log.h"
#include "esp_gap_bt_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bt_app_av.h"  // for s_connection_state

static const char *TAG = "reconnect";

#define NVS_BT_NAMESPACE "bt_rec"
#define LAST_BDA_KEY     "last_bda"

// Convert BDA to null-terminated hex string (lowercase)
static void bda_to_str(const uint8_t *bda, char *str) {
    sprintf(str, "%02x%02x%02x%02x%02x%02x",
            bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
}

// Convert hex string to BDA
static void str_to_bda(const char *str, uint8_t *bda) {
    sscanf(str, "%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx",
           &bda[0], &bda[1], &bda[2], &bda[3], &bda[4], &bda[5]);
}

// Check if BDA is in current bond list
static bool is_bda_bonded(const uint8_t *bda) {
    int dev_num = esp_bt_gap_get_bond_device_num();
    if (dev_num <= 0) return false;

    esp_bd_addr_t *dev_list = (esp_bd_addr_t *)malloc(sizeof(esp_bd_addr_t) * dev_num);
    if (!dev_list) {
        ESP_LOGE(TAG, "Failed to alloc bond list");
        return false;
    }

    esp_err_t ret = esp_bt_gap_get_bond_device_list(&dev_num, dev_list);
    bool found = false;
    if (ret == ESP_OK) {
        for (int i = 0; i < dev_num; i++) {
            if (memcmp(bda, dev_list[i], ESP_BD_ADDR_LEN) == 0) {
                found = true;
                break;
            }
        }
    }

    free(dev_list);
    return found;
}

// Attempt to reconnect to a single BDA (blocking)
static bool try_reconnect_bda(const uint8_t *bda) {
    ESP_LOGI(TAG, "Attempting reconnect to " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(bda));

    esp_err_t ret = esp_a2d_sink_connect((uint8_t *)bda);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "esp_a2d_sink_connect failed: %d", ret);
        return false;
    }

    // Wait for connection state change
    // todo: can wait forever
    while (s_connection_state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    while (true) {
        if (s_connection_state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
            ESP_LOGI(TAG, "Connected to " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(bda));
            return true;
        } else if (s_connection_state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            ESP_LOGI(TAG, "Failed to connect to " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(bda));
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Reconnect task: only tries the last known device
static void reconnect_task(void *arg) {
    ESP_LOGI(TAG, "Starting reconnect task");

    if (s_connection_state != ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
        ESP_LOGI(TAG, "Already connected, skipping reconnect");
        vTaskDelete(NULL);
        return;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_BT_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %d", err);
        vTaskDelete(NULL);
        return;
    }

    size_t len = 13; // 12 chars + null terminator
    char bda_str[13];
    err = nvs_get_str(handle, LAST_BDA_KEY, bda_str, &len);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No last known device in NVS");
        nvs_close(handle);
        vTaskDelete(NULL);
        return;
    }

    uint8_t bda[ESP_BD_ADDR_LEN];
    str_to_bda(bda_str, bda);

    // Validate bonding
    if (!is_bda_bonded(bda)) {
        ESP_LOGI(TAG, "Last device no longer bonded. Removing.");
        nvs_erase_key(handle, LAST_BDA_KEY);
        nvs_commit(handle);
        nvs_close(handle);
        vTaskDelete(NULL);
        return;
    }

    nvs_close(handle);

    // Try to reconnect
    if (try_reconnect_bda(bda)) {
        ESP_LOGI(TAG, "Reconnect succeeded");
    } else {
        ESP_LOGW(TAG, "Reconnect failed");
    }

    vTaskDelete(NULL);
}

// Public API: store ONLY the last connected device
esp_err_t bt_reconnect_add_candidate(const uint8_t *bda) {
    if (!bda) return ESP_ERR_INVALID_ARG;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_BT_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %d", err);
        return err;
    }

    char key[13];
    bda_to_str(bda, key);
    err = nvs_set_str(handle, LAST_BDA_KEY, key);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
        ESP_LOGI(TAG, "Saved last known device: %s", key);
    } else {
        ESP_LOGE(TAG, "Failed to save last device: %d", err);
    }

    nvs_close(handle);
    return err;
}

// Public API: remove last known device
esp_err_t bt_reconnect_remove_candidate(const uint8_t *bda) {
    // We ignore 'bda' -- we only track one device
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_BT_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %d", err);
        return err;
    }

    err = nvs_erase_key(handle, LAST_BDA_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    } else if (err == ESP_OK) {
        nvs_commit(handle);
        ESP_LOGI(TAG, "Removed last known device");
    } else {
        ESP_LOGE(TAG, "Failed to remove last device: %d", err);
    }

    nvs_close(handle);
    return err;
}

// Start reconnect task
esp_err_t bt_reconnect_start_task(void) {
    BaseType_t ret = xTaskCreate(reconnect_task, "reconnect_task", 3072, NULL, tskIDLE_PRIORITY + 2, NULL);
    return (ret == pdPASS) ? ESP_OK : ESP_FAIL;
}