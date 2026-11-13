#ifndef BLE_CONTROLLER_H
#define BLE_CONTROLLER_H

/**
 * @brief 配置 C3 主控的蓝牙回调、设备名、通信服务和广播重试定时器。
 * @note 在 nimble_port_init() 成功后、启动主机任务前调用一次。
 *       广播由主机与控制器同步完成后的回调启动，本函数不直接启动广播。
 */
void ble_controller_init(void);

#endif
