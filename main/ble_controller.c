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

/**
 * @file ble_controller.c
 * @brief C3 外围设备：广播通信服务，处理 PING/PONG 和 SG90 目标角度设置。
 * @note GAP（Generic Access Profile，通用访问规范）管理广播与连接；
 *       GATT（Generic Attribute Profile，通用属性规范）管理服务、特征和数据访问。
 */

#include <assert.h>
#include <string.h>

#include "ble_controller.h"
#include "ble_protocol.h"
#include "servo_control.h"
#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "ota_update.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/** 日志标签，用于区分主控模块的输出。 */
static const char *TAG = "ble_controller";
/** GAP 设备名，同时放入扫描响应包。 */
static const char DEVICE_NAME[] = "BLE-C3-Controller";
/** 响应常量；数组含 '\0'，回复时只取前四字节。 */
static const char PONG[] = BLE_LINK_PONG;
/** 对外广播并注册的服务标识。 */
static const ble_uuid128_t SERVICE_UUID = BLE_LINK_SERVICE_UUID;
/** 收发特征标识。 */
static const ble_uuid128_t CHARACTERISTIC_UUID = BLE_LINK_CHARACTERISTIC_UUID;

/* 以下状态及广播重试回调均由 NimBLE 主机事件队列管理。 */
/** 本机蓝牙地址类型，主机同步后确定。 */
static uint8_t s_own_address_type;
/** 协议栈注册服务时回填的特征值句柄，用于识别订阅事件。 */
static uint16_t s_value_handle;
/** 当前单连接是否开启通知；角度命令要求先订阅，以便返回设置回执。 */
static bool s_notifications_enabled;
/** 广播配置或启动失败后，延迟一秒重试。 */
static struct ble_npl_callout s_advertise_retry;

static int gap_event(struct ble_gap_event *event, void *argument);
static void advertise(struct ble_npl_event *event);

/**
 * @brief 处理 PING/PONG 和 Sddd 角度命令；成功更新 PWM 后通知 Addd。
 * @param connection_handle 访问所属连接，用于向写入方通知角度设置回执。
 * @param attribute_handle 当前特征值句柄，用于通知协议栈该特征已更新。
 * @param context 访问上下文，op 区分读写，om 为协议栈提供的链式数据缓冲区。
 * @param argument 注册特征时传入的用户参数，本回调未使用。
 * @return 0 表示访问成功；其他值为 ATT（Attribute Protocol，属性协议）错误码。
 * @note 收到的数据不保证内存连续或带字符串结束符，必须先检查长度再复制比较。
 */
static int characteristic_access(uint16_t connection_handle, uint16_t attribute_handle,
                                 struct ble_gatt_access_ctxt *context, void *argument)
{
    (void)argument;

    if (context->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        /* 读取请求和通知构造均走此路径：把固定四字节 PONG 追加到回复缓冲区。 */
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

    /* request 保存四字节请求，不含 '\0'；result 是链式缓冲区复制操作的结果。 */
    char request[BLE_LINK_MESSAGE_LENGTH];
    int result = ble_hs_mbuf_to_flat(context->om, request, sizeof(request), NULL);
    if (result != 0)
    {
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (memcmp(request, BLE_LINK_PING, sizeof(request)) == 0)
    {
        ESP_LOGI(TAG, "RX: PING");
        /* 原有 PING/PONG 行为保持不变，协议栈只向已订阅的连接调度通知。 */
        ble_gatts_chr_updated(attribute_handle);
        ESP_LOGI(TAG, "TX: PONG queued");
        return 0;
    }

    uint16_t angle;
    if (request[0] != BLE_LINK_SERVO_SET ||
        !ble_link_decode_angle(request, sizeof(request), &angle))
    {
        return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
    }
    if (!s_notifications_enabled)
    {
        return BLE_ATT_ERR_UNLIKELY;
    }

    char response[BLE_LINK_MESSAGE_LENGTH];
    ble_link_encode_angle(BLE_LINK_SERVO_ACK, angle, response);
    /* 先准备本次回执的独立缓冲区，避免内容被下一条 PING 或角度命令覆盖。 */
    struct os_mbuf *notification = ble_hs_mbuf_from_flat(response, sizeof(response));
    if (notification == NULL)
    {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    esp_err_t servo_result = servo_set_angle(angle);
    if (servo_result != ESP_OK)
    {
        os_mbuf_free_chain(notification);
        ESP_LOGE(TAG, "Servo PWM update failed; status=%d", servo_result);
        return BLE_ATT_ERR_UNLIKELY;
    }

    ESP_LOGI(TAG, "RX: S%03u; PWM configured", (unsigned int)angle);
    /* 此接口无论成功失败都会接管 mbuf；返回后不得再次释放。 */
    result = ble_gatts_notify_custom(connection_handle, attribute_handle, notification);
    if (result != 0)
    {
        ESP_LOGW(TAG, "Servo target set, but acknowledgement failed; status=%d", result);
        return BLE_ATT_ERR_UNLIKELY;
    }
    ESP_LOGI(TAG, "TX: A%03u queued (PWM acknowledgement)", (unsigned int)angle);
    return 0;
}

/**
 * @brief 通信特征注册表：同一特征支持读取、写入和通知，由协议栈分配值句柄。
 * @note 末尾全零条目为 SDK 要求的结束标记；通知配置描述符由协议栈自动添加。
 */
static const struct ble_gatt_chr_def CHARACTERISTICS[] = {
    {
        .uuid = &CHARACTERISTIC_UUID.u,
        .access_cb = characteristic_access,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_value_handle,
    },
    {0},
};

/** @brief 服务注册表：将通信特征挂在主服务下，末尾全零条目表示表结束。 */
static const struct ble_gatt_svc_def SERVICES[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &SERVICE_UUID.u,
        .characteristics = CHARACTERISTICS,
    },
    {0},
};

/**
 * @brief 配置服务广播和设备名扫描响应，启动可连接广播，失败时一秒后重试。
 * @param event 重试定时器的事件；直接调用时可传 NULL，函数不读取此参数。
 * @note 服务标识保留在广播包中，S3 无须主动请求扫描响应即可匹配本设备。
 */
static void advertise(struct ble_npl_event *event)
{
    (void)event;
    /* fields 描述待编码的广播字段，后续清零并复用于单独的扫描响应包。 */
    struct ble_hs_adv_fields fields = {0};
    /* parameters 控制是否可连接、是否可发现，未显式设置的参数使用协议栈默认值。 */
    struct ble_gap_adv_params parameters = {0};

    /* 同时广播舵机服务和手机 OTA 服务，供 S3 和 APP 分别筛选。 */
    ble_uuid16_t ota_uuid = BLE_UUID16_INIT(OTA_SERVICE_UUID16);
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&SERVICE_UUID;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;
    fields.uuids16 = &ota_uuid;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
    int result = ble_gap_adv_set_fields(&fields); /* 广播配置或启动的返回码，0 表示成功。 */
    if (result != 0)
    {
        goto retry;
    }

    /* 完整设备名放入独立扫描响应包，避免与服务 UUID 共用广播包时超出长度限制。 */
    memset(&fields, 0, sizeof(fields));
    fields.name = (uint8_t *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;
    result = ble_gap_adv_rsp_set_fields(&fields);
    if (result != 0)
    {
        goto retry;
    }

    parameters.conn_mode = BLE_GAP_CONN_MODE_UND; /* 非定向可连接，接受中央设备发起连接。 */
    parameters.disc_mode = BLE_GAP_DISC_MODE_GEN; /* 通用可发现模式。 */
    result =
        ble_gap_adv_start(s_own_address_type, NULL, BLE_HS_FOREVER, &parameters, gap_event, NULL);
    if (result == 0)
    {
        ESP_LOGI(TAG, "Advertising as %s", DEVICE_NAME);
        ota_update_ble_ready();
        return;
    }

retry:
    ESP_LOGW(TAG, "Advertising failed; status=%d, retrying in 1 s", result);
    ble_npl_callout_reset(&s_advertise_retry, ble_npl_time_ms_to_ticks32(1000));
}

/**
 * @brief 处理连接状态及通知订阅变化，连接失败、断连或广播结束后恢复广播。
 * @param event 事件类型及其对应数据，只读取与 type 匹配的成员。
 * @param argument 注册 GAP 回调时传入的用户参数，本回调未使用。
 * @return 始终返回 0，表示本回调已处理或忽略该事件。
 * @note 单连接订阅状态同时用于拒绝无法返回回执的角度命令。
 */
static int gap_event(struct ble_gap_event *event, void *argument)
{
    (void)argument;
    ota_update_gap_event(event);

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
            s_notifications_enabled = false;
            ESP_LOGI(TAG, "Disconnected; reason=%d", event->disconnect.reason);
            advertise(NULL);
            return 0;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            advertise(NULL);
            return 0;

        case BLE_GAP_EVENT_SUBSCRIBE:
            if (event->subscribe.attr_handle == s_value_handle)
            {
                s_notifications_enabled = event->subscribe.cur_notify;
                ESP_LOGI(TAG, "PONG notifications %s",
                         event->subscribe.cur_notify ? "enabled" : "disabled");
            }
            return 0;

        default:
            return 0;
    }
}

/**
 * @brief 主机异常复位时停止广播重试，避免协议栈未就绪时继续发起广播。
 * @param reason NimBLE 上报的复位原因码，用于日志诊断。
 * @note 等待主机重新同步后，由 on_sync() 恢复广播。
 */
static void on_reset(int reason)
{
    s_notifications_enabled = false;
    ota_update_link_reset();
    ble_npl_callout_stop(&s_advertise_retry);
    ESP_LOGE(TAG, "NimBLE reset; reason=%d", reason);
}

/**
 * @brief 主机与控制器同步后准备本机地址，并启动广播等待 S3 连接。
 * @note 地址准备成功后才能发起 GAP 操作；地址类型推断时不启用地址隐私。
 */
static void on_sync(void)
{
    /* 地址准备和地址类型推断的结果，0 表示成功。 */
    int result = ble_hs_util_ensure_addr(0);
    assert(result == 0);
    result = ble_hs_id_infer_auto(0, &s_own_address_type);
    assert(result == 0);
    advertise(NULL);
}

/** @brief 初始化主控的服务和广播资源，具体调用时机见 ble_controller.h。 */
void ble_controller_init(void)
{
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    /* 先注册官方 GAP/GATT 基础服务，再注册本项目的通信服务。 */
    ble_svc_gap_init();
    ble_svc_gatt_init();

    int result = ble_svc_gap_device_name_set(DEVICE_NAME); /* 名称配置及服务注册结果。 */
    assert(result == 0);
    /* 先统计服务表需要的协议栈资源，再将服务表加入待注册列表。 */
    result = ble_gatts_count_cfg(SERVICES);
    assert(result == 0);
    result = ble_gatts_add_svcs(SERVICES);
    assert(result == 0);

    /* 使用主机默认事件队列，广播重试与 GAP 回调串行执行，无须额外应用任务。 */
    ble_npl_callout_init(&s_advertise_retry, nimble_port_get_dflt_eventq(), advertise, NULL);
    ESP_LOGI(TAG, "Controller initialized; waiting for PING or servo commands");
}
