#ifndef RECONNECT_H
#define RECONNECT_H

#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Annotate a BDA as a reconnect candidate (store in NVS).
 *
 * @param bda 6-byte Bluetooth Device Address.
 * @return ESP_OK on success.
 */
esp_err_t bt_reconnect_add_candidate(const uint8_t *bda);

/**
 * @brief Remove a BDA from reconnect candidates.
 *
 * @param bda 6-byte Bluetooth Device Address.
 * @return ESP_OK if removed or not present.
 */
esp_err_t bt_reconnect_remove_candidate(const uint8_t *bda);

/**
 * @brief Start the reconnect task (if not already running).
 *
 * This function is idempotent. The task runs once and exits after
 * attempting all candidates or when connected.
 *
 * @return ESP_OK on success.
 */
esp_err_t bt_reconnect_start_task(void);

#endif // RECONNECT_H