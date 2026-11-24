/* SPDX-License-Identifier: Apache-2.0 */

#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#include <stdbool.h>

#include "esp_err.h"

struct ble_gap_event;

#define OTA_SERVICE_UUID16 0x8018

/* Register the OTA GATT service before starting the NimBLE host. */
esp_err_t ota_update_init(void);

/* Forward GAP events for the peripheral connection to the phone. */
void ota_update_gap_event(const struct ble_gap_event *event);
void ota_update_link_reset(void);

/* Boot verification checks that the application's normal BLE operation started. */
void ota_update_ble_ready(void);
void ota_update_confirm_boot(bool hardware_ready);

#endif
