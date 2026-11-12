/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Started from tools/templates/sample_project; BLE startup follows bleprph.
 */

#include "ble_controller.h"
#include "esp_err.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"

static void ble_host_task(void *argument)
{
    (void)argument;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void app_main(void)
{
    /* Keep the official recovery path for incompatible or full NVS pages. */
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(result);
    ESP_ERROR_CHECK(nimble_port_init());

    ble_controller_init();
    nimble_port_freertos_init(ble_host_task);
}
