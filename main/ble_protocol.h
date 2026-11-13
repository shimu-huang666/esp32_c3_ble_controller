/**
 * @file ble_protocol.h
 * @brief 两端共用的 BLE（Bluetooth Low Energy，低功耗蓝牙）通信标识与消息定义。
 * @note 两个独立工程各保留一份，修改协议时必须同步更新。
 */

#ifndef BLE_PROTOCOL_H
#define BLE_PROTOCOL_H

#include "host/ble_uuid.h"

/* 服务 UUID（Universally Unique Identifier，通用唯一标识符），用于扫描匹配服务。
 * 文本形式：d58f7e60-4c93-4f7b-9ba7-664e114dab7c。
 * NimBLE 要求 128 位 UUID 按最低有效字节在前的顺序初始化，与文本显示顺序相反。
 */
#define BLE_LINK_SERVICE_UUID                                                                      \
    BLE_UUID128_INIT(0x7c, 0xab, 0x4d, 0x11, 0x4e, 0x66, 0xa7, 0x9b, 0x7b, 0x4f, 0x93, 0x4c, 0x60, \
                     0x7e, 0x8f, 0xd5)

/* 通信特征 UUID：d58f7e61-4c93-4f7b-9ba7-664e114dab7c。
 * S3 向此特征写入 PING，并订阅它的 PONG 通知。
 */
#define BLE_LINK_CHARACTERISTIC_UUID                                                               \
    BLE_UUID128_INIT(0x7c, 0xab, 0x4d, 0x11, 0x4e, 0x66, 0xa7, 0x9b, 0x7b, 0x4f, 0x93, 0x4c, 0x61, \
                     0x7e, 0x8f, 0xd5)

#define BLE_LINK_PING "PING"      /**< S3 发给 C3 的请求文本。 */
#define BLE_LINK_PONG "PONG"      /**< C3 回复 S3 的响应文本。 */
#define BLE_LINK_MESSAGE_LENGTH 4 /**< 无线消息长度，单位为字节，不含字符串结束符 '\0'。 */

#endif
