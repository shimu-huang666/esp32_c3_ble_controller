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

/**
 * @brief 运行 NimBLE 蓝牙主机的事件循环，处理连接、收发及定时回调。
 * @param argument FreeRTOS 任务入口参数，本任务未使用。
 * @note nimble_port_run() 持续处理事件，协议栈停止后才返回并清理主机任务。
 */
static void ble_host_task(void *argument)
{
    (void)argument;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

/**
 * @brief 应用入口：初始化持久存储和蓝牙协议栈，注册服务后启动主机任务。
 * @note 必须先注册服务和回调，再启动主机，避免事件到来时尚未就绪。
 */
void app_main(void)
{
    /* NVS（Non-Volatile Storage，非易失存储）初始化结果，供后续错误检查使用。 */
    esp_err_t result = nvs_flash_init();
    if (result == ESP_ERR_NVS_NO_FREE_PAGES || result == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        /* 沿用官方恢复流程：页耗尽或版本不兼容时，擦除默认 NVS 分区后重新初始化。 */
        ESP_ERROR_CHECK(nvs_flash_erase());
        result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(result);
    ESP_ERROR_CHECK(nimble_port_init());

    ble_controller_init();
    nimble_port_freertos_init(ble_host_task);
}
