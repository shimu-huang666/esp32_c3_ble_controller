#ifndef BLE_PROTOCOL_H
#define BLE_PROTOCOL_H

#include "host/ble_uuid.h"

/* NimBLE stores 128-bit UUID bytes least-significant byte first. */
/* Service: d58f7e60-4c93-4f7b-9ba7-664e114dab7c */
#define BLE_LINK_SERVICE_UUID                                                                      \
    BLE_UUID128_INIT(0x7c, 0xab, 0x4d, 0x11, 0x4e, 0x66, 0xa7, 0x9b, 0x7b, 0x4f, 0x93, 0x4c, 0x60, \
                     0x7e, 0x8f, 0xd5)

/* Characteristic: d58f7e61-4c93-4f7b-9ba7-664e114dab7c */
#define BLE_LINK_CHARACTERISTIC_UUID                                                               \
    BLE_UUID128_INIT(0x7c, 0xab, 0x4d, 0x11, 0x4e, 0x66, 0xa7, 0x9b, 0x7b, 0x4f, 0x93, 0x4c, 0x61, \
                     0x7e, 0x8f, 0xd5)

#define BLE_LINK_PING "PING"
#define BLE_LINK_PONG "PONG"
#define BLE_LINK_MESSAGE_LENGTH 4

#endif
