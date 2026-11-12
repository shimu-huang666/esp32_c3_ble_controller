/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership. The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 * Adapted from ESP-IDF v6.1 examples/bluetooth/nimble/bleprph:
 * keep GAP advertising and GATT access; replace demo services with PING/PONG.
 */

#include <assert.h>
#include <string.h>

#include "ble_controller.h"
#include "ble_protocol.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble_controller";
static const char DEVICE_NAME[] = "BLE-C3-Controller";
static const char PONG[] = BLE_LINK_PONG;
static const ble_uuid128_t SERVICE_UUID = BLE_LINK_SERVICE_UUID;
static const ble_uuid128_t CHARACTERISTIC_UUID = BLE_LINK_CHARACTERISTIC_UUID;

static uint8_t s_own_address_type;
static uint16_t s_value_handle;
static struct ble_npl_callout s_advertise_retry;

static int gap_event(struct ble_gap_event *event, void *argument);
static void advertise(struct ble_npl_event *event);

static int characteristic_access(uint16_t connection_handle, uint16_t attribute_handle,
                                 struct ble_gatt_access_ctxt *context, void *argument)
{
    (void)connection_handle;
    (void)argument;

    if (context->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        /* NimBLE also uses this read path to build the queued notification. */
        int result = os_mbuf_append(context->om, PONG, BLE_LINK_MESSAGE_LENGTH);
        return result == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    if (context->op != BLE_GATT_ACCESS_OP_WRITE_CHR)
    {
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (OS_MBUF_PKTLEN(context->om) != BLE_LINK_MESSAGE_LENGTH)
    {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    char request[BLE_LINK_MESSAGE_LENGTH];
    int result = ble_hs_mbuf_to_flat(context->om, request, sizeof(request), NULL);
    if (result != 0)
    {
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (memcmp(request, BLE_LINK_PING, sizeof(request)) != 0)
    {
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }

    ESP_LOGI(TAG, "RX: PING");
    /* Official API schedules a notification for subscribed clients only. */
    ble_gatts_chr_updated(attribute_handle);
    ESP_LOGI(TAG, "TX: PONG queued");
    return 0;
}

static const struct ble_gatt_chr_def CHARACTERISTICS[] = {
    {
        .uuid = &CHARACTERISTIC_UUID.u,
        .access_cb = characteristic_access,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_value_handle,
    },
    {0},
};

static const struct ble_gatt_svc_def SERVICES[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &SERVICE_UUID.u,
        .characteristics = CHARACTERISTICS,
    },
    {0},
};

static void advertise(struct ble_npl_event *event)
{
    (void)event;
    struct ble_hs_adv_fields fields = {0};
    struct ble_gap_adv_params parameters = {0};

    /* Flags + one 128-bit UUID fit in the 31-byte legacy advertisement. */
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&SERVICE_UUID;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    int result = ble_gap_adv_set_fields(&fields);
    if (result != 0)
    {
        goto retry;
    }

    /* Put the complete device name in the separate scan-response packet. */
    memset(&fields, 0, sizeof(fields));
    fields.name = (uint8_t *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;
    result = ble_gap_adv_rsp_set_fields(&fields);
    if (result != 0)
    {
        goto retry;
    }

    parameters.conn_mode = BLE_GAP_CONN_MODE_UND;
    parameters.disc_mode = BLE_GAP_DISC_MODE_GEN;
    result =
        ble_gap_adv_start(s_own_address_type, NULL, BLE_HS_FOREVER, &parameters, gap_event, NULL);
    if (result == 0)
    {
        ESP_LOGI(TAG, "Advertising as %s", DEVICE_NAME);
        return;
    }

retry:
    ESP_LOGW(TAG, "Advertising failed; status=%d, retrying in 1 s", result);
    ble_npl_callout_reset(&s_advertise_retry, ble_npl_time_ms_to_ticks32(1000));
}

static int gap_event(struct ble_gap_event *event, void *argument)
{
    (void)argument;

    switch (event->type)
    {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0)
            {
                ESP_LOGI(TAG, "Connected; handle=%u", event->connect.conn_handle);
            }
            else
            {
                ESP_LOGW(TAG, "Connection failed; status=%d", event->connect.status);
                advertise(NULL);
            }
            return 0;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Disconnected; reason=%d", event->disconnect.reason);
            advertise(NULL);
            return 0;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            advertise(NULL);
            return 0;

        case BLE_GAP_EVENT_SUBSCRIBE:
            if (event->subscribe.attr_handle == s_value_handle)
            {
                ESP_LOGI(TAG, "PONG notifications %s",
                         event->subscribe.cur_notify ? "enabled" : "disabled");
            }
            return 0;

        default:
            return 0;
    }
}

static void on_reset(int reason)
{
    ble_npl_callout_stop(&s_advertise_retry);
    ESP_LOGE(TAG, "NimBLE reset; reason=%d", reason);
}

static void on_sync(void)
{
    int result = ble_hs_util_ensure_addr(0);
    assert(result == 0);
    result = ble_hs_id_infer_auto(0, &s_own_address_type);
    assert(result == 0);
    advertise(NULL);
}

void ble_controller_init(void)
{
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_svc_gap_init();
    ble_svc_gatt_init();

    int result = ble_svc_gap_device_name_set(DEVICE_NAME);
    assert(result == 0);
    result = ble_gatts_count_cfg(SERVICES);
    assert(result == 0);
    result = ble_gatts_add_svcs(SERVICES);
    assert(result == 0);

    ble_npl_callout_init(&s_advertise_retry, nimble_port_get_dflt_eventq(), advertise, NULL);
    ESP_LOGI(TAG, "Controller initialized; waiting for PING");
}
