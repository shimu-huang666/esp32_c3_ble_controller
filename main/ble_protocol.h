/**
 * @file ble_protocol.h
 * @brief 两端共用的 BLE（Bluetooth Low Energy，低功耗蓝牙）通信标识与消息定义。
 * @note 两个独立工程各保留一份，修改协议时必须同步更新。
 */

#ifndef BLE_PROTOCOL_H
#define BLE_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
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

/** 四字节角度消息：Sddd 设置角度，Addd 确认 PWM 已设置；ddd 为 000–180。 */
#define BLE_LINK_SERVO_SET 'S'
#define BLE_LINK_SERVO_ACK 'A'
#define BLE_LINK_SERVO_MAX_ANGLE 180U

/** 解码完整角度消息；失败时不改变输出角度，调用方另行检查请求/回执前缀。 */
static inline bool ble_link_decode_angle(const char *message, size_t length, uint16_t *angle)
{
    if (message == NULL || angle == NULL || length != BLE_LINK_MESSAGE_LENGTH ||
        (message[0] != BLE_LINK_SERVO_SET && message[0] != BLE_LINK_SERVO_ACK))
    {
        return false;
    }
    uint16_t value = 0;
    for (size_t index = 1; index < BLE_LINK_MESSAGE_LENGTH; ++index)
    {
        if (message[index] < '0' || message[index] > '9')
        {
            return false;
        }
        value = (uint16_t)(value * 10 + (message[index] - '0'));
    }
    if (value > BLE_LINK_SERVO_MAX_ANGLE)
    {
        return false;
    }
    *angle = value;
    return true;
}

/** 编码到调用方提供的四字节缓冲区，不追加字符串结束符。 */
static inline bool ble_link_encode_angle(char prefix, uint16_t angle,
                                         char message[BLE_LINK_MESSAGE_LENGTH])
{
    if (message == NULL || angle > BLE_LINK_SERVO_MAX_ANGLE ||
        (prefix != BLE_LINK_SERVO_SET && prefix != BLE_LINK_SERVO_ACK))
    {
        return false;
    }
    message[0] = prefix;
    message[1] = (char)('0' + angle / 100);
    message[2] = (char)('0' + angle / 10 % 10);
    message[3] = (char)('0' + angle % 10);
    return true;
}

#endif
