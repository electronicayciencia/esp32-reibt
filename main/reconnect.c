#include "reconnect.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_log.h"
#include "esp_gap_bt_api.h"
//#include "esp_a2dp_api.h"
//#include "esp_avrc_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bt_app_av.h"  // for s_connection_state

static const char *TAG = "reconnect";

#define NVS_BT_NAMESPACE "bt_rec"

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

// Attempt to reconnect to a single BDA (blocking, up to 5 seconds)
static bool try_reconnect_bda(const uint8_t *bda) {
    ESP_LOGI(TAG, "Attempting reconnect to " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(bda));

    // In A2DP sink, reconnection is typically initiated by the source.
    // However, we can trigger ACL connection to prompt the source.
    esp_err_t ret = esp_a2d_sink_connect((uint8_t *)bda);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "esp_a2d_sink_connect failed: %d", ret);
        return false;
    }

    // Wait until something happens
    while (s_connection_state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    while (true) {
        if (s_connection_state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
            ESP_LOGI(TAG, "Connected to " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(bda));
            return true;
        } else if (s_connection_state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            ESP_LOGI(TAG, "Can't connect to " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(bda));
            return false;
        } else {
            ESP_LOGI(TAG, "Waiting for " ESP_BD_ADDR_STR, ESP_BD_ADDR_HEX(bda));
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Reconnect task
static void reconnect_task(void *arg) {
    ESP_LOGI(TAG, "Starting reconnect task");

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_BT_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %d", err);
        vTaskDelete(NULL);
        return;
    }

    // Only run if currently disconnected
    if (s_connection_state != ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
        ESP_LOGI(TAG, "Already connected, skipping reconnect");
        nvs_close(handle);
        vTaskDelete(NULL);
        return;
    }

    // loop until it connects
    while (s_connection_state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {

        // Iterate all keys in namespace
        nvs_iterator_t it = NULL;
        uint8_t candidates = 0;
        esp_err_t res = nvs_entry_find("nvs", NVS_BT_NAMESPACE, NVS_TYPE_ANY, &it);
        while (res == ESP_OK && s_connection_state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            bool delete_candidate = false;
            candidates++;

            nvs_entry_info_t info;
            nvs_entry_info(it, &info);

            // Key name is BDA hex string
            uint8_t bda[ESP_BD_ADDR_LEN];
            str_to_bda(info.key, bda);

            // Check if still bonded
            ESP_LOGI(TAG, "Candidate: %s", info.key);
            if (!is_bda_bonded(bda)) {
                ESP_LOGI(TAG, "Removing unbonded candidate: %s", info.key);
                delete_candidate = true;
            } else {
                // Try to reconnect
                if (try_reconnect_bda(bda)) {
                    // Success! Exit loop
                    break;
                }
                // Continue to next candidate
            }

            res = nvs_entry_next(&it);

            if (delete_candidate) {
                bt_reconnect_remove_candidate(bda);
            }
            
            // Wait for any client to connect to us
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
        nvs_release_iterator(it);

        if (candidates == 0) {
            ESP_LOGI(TAG, "No candidates. Reconnect task finished");
            vTaskDelete(NULL);            
        }

        // Next try for all
        ESP_LOGI(TAG, "No more candidates to try");
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
    nvs_close(handle);

    ESP_LOGI(TAG, "Reconnect task finished");
    vTaskDelete(NULL);
}

// Public API implementations

esp_err_t bt_reconnect_add_candidate(const uint8_t *bda) {
    if (!bda) return ESP_ERR_INVALID_ARG;

    char key[13]; // 12 hex chars + null
    bda_to_str(bda, key);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_BT_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %d", err);
        return err;
    }

    // Store dummy value (1). Key name is the BDA.
    err = nvs_set_i8(handle, key, 1);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
        ESP_LOGI(TAG, "Added reconnect candidate: %s", key);
    } else {
        ESP_LOGE(TAG, "Failed to store candidate %s: %d", key, err);
    }

    nvs_close(handle);
    return err;
}

esp_err_t bt_reconnect_remove_candidate(const uint8_t *bda) {
    if (!bda) return ESP_ERR_INVALID_ARG;

    char key[13];
    bda_to_str(bda, key);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_BT_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %d", err);
        return err;
    }

    err = nvs_erase_key(handle, key);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK; // Not an error if not present
    } else if (err == ESP_OK) {
        nvs_commit(handle);
        ESP_LOGI(TAG, "Removed reconnect candidate: %s", key);
    } else {
        ESP_LOGE(TAG, "Failed to remove candidate %s: %d", key, err);
    }

    nvs_close(handle);
    return err;
}

esp_err_t bt_reconnect_start_task(void) {
    esp_err_t err = xTaskCreate(reconnect_task, "reconnect_task", 3072, NULL, tskIDLE_PRIORITY + 2, NULL);
    return err;
}