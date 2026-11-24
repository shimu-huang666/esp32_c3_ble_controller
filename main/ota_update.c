/* SPDX-License-Identifier: Apache-2.0 */

/*
 * Wire format follows Espressif's BLE OTA protocol and ESP BLE OTA Android client.
 * https://components.espressif.com/components/espressif/ble_ota/versions/0.1.18/examples/ble_ota
 */

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#include "ota_update.h"

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_hs.h"

enum
{
    OTA_FIRMWARE_UUID = 0x8020,
    OTA_PROGRESS_UUID = 0x8021,
    OTA_COMMAND_UUID = 0x8022,
    OTA_CUSTOMER_UUID = 0x8023,
    OTA_START = 1,
    OTA_END = 2,
    OTA_COMMAND_ACK = 3,
    OTA_COMMAND_REFUSED = 1,
    OTA_SECTOR_CRC_ERROR = 1,
    OTA_SECTOR_INDEX_ERROR = 2,
    OTA_SECTOR_LENGTH_ERROR = 3,
    OTA_SECTOR_SIZE = 4096,
    OTA_PACKET_HEADER = 3,
    OTA_LAST_PACKET = 0xff,
    OTA_FRAME_SIZE = 20,
    OTA_CRC_OFFSET = 18,
    OTA_MAX_ATT_PAYLOAD = 514,
};

typedef struct
{
    unsigned int generation;
    uint16_t conn_handle;
    uint16_t attr_handle;
    uint16_t length;
    uint8_t data[OTA_MAX_ATT_PAYLOAD];
} ota_request_t;

typedef struct
{
    const esp_partition_t *partition;
    esp_ota_handle_t handle;
    unsigned int generation;
    size_t total;
    size_t written;
    size_t sector_used;
    uint16_t sector_index;
    uint8_t next_packet;
} ota_transfer_t;

static const char *TAG = "ble_ota";
static QueueHandle_t s_requests;
static TaskHandle_t s_writer;
static SemaphoreHandle_t s_ble_ready;
static atomic_uint s_generation;

/* Only the NimBLE host changes the phone connection and reads the GATT handles. */
static uint16_t s_phone = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_command_handle;
static uint16_t s_firmware_handle;

/* Only the flash writer uses this buffer and the current transfer. */
static uint8_t s_sector[OTA_SECTOR_SIZE];
static ota_transfer_t s_transfer;

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)(data[0] | (data[1] << 8));
}

static void write_le16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

/* CRC-16/XMODEM. The phone uses zero as the initial value. */
static uint16_t ota_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0;
    for (size_t index = 0; index < length; ++index)
    {
        crc ^= (uint16_t)data[index] << 8;
        for (unsigned int bit = 0; bit < 8; ++bit)
        {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static bool link_is_current(const ota_request_t *request)
{
    return request->generation == atomic_load(&s_generation);
}

static void send_reply(const ota_request_t *request, uint16_t first, uint16_t second,
                       uint16_t third)
{
    if (!link_is_current(request))
    {
        return;
    }

    uint8_t reply[OTA_FRAME_SIZE] = {0};
    write_le16(reply, first);
    write_le16(reply + 2, second);
    write_le16(reply + 4, third);
    write_le16(reply + OTA_CRC_OFFSET, ota_crc16(reply, OTA_CRC_OFFSET));

    struct os_mbuf *value = ble_hs_mbuf_from_flat(reply, sizeof(reply));
    if (value == NULL)
    {
        ESP_LOGW(TAG, "No buffer for OTA acknowledgement");
        return;
    }

    ulTaskNotifyTake(pdTRUE, 0);
    /* Wait in the writer so a following reply cannot overtake this indication. */
    int result = ble_gatts_indicate_custom(request->conn_handle, request->attr_handle, value);
    if (result != 0 || ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(2000)) == 0)
    {
        ESP_LOGW(TAG, "OTA acknowledgement not confirmed (%d)", result);
    }
}

static void abort_transfer(void)
{
    if (s_transfer.partition != NULL)
    {
        esp_ota_abort(s_transfer.handle);
        s_transfer = (ota_transfer_t){0};
    }
}

static esp_err_t start_transfer(const ota_request_t *request)
{
    abort_transfer();
    const size_t size =
        read_le16(request->data + 2) | ((uint32_t)read_le16(request->data + 4) << 16);
    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    if (partition == NULL || size == 0 || size > partition->size)
    {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_ota_handle_t handle;
    esp_err_t result = esp_ota_begin(partition, OTA_WITH_SEQUENTIAL_WRITES, &handle);
    if (result == ESP_OK)
    {
        s_transfer = (ota_transfer_t){
            .partition = partition,
            .handle = handle,
            .generation = request->generation,
            .total = size,
        };
        ESP_LOGI(TAG, "Receiving %zu bytes into %s", size, partition->label);
    }
    return result;
}

static esp_err_t finish_transfer(const ota_request_t *request)
{
    if (s_transfer.partition == NULL || s_transfer.written != s_transfer.total)
    {
        abort_transfer();
        return ESP_ERR_INVALID_SIZE;
    }

    const esp_partition_t *partition = s_transfer.partition;
    esp_err_t result = esp_ota_end(s_transfer.handle);
    /* esp_ota_end consumes the handle, including on a verification error. */
    s_transfer = (ota_transfer_t){0};
    if (result != ESP_OK)
    {
        return result;
    }

    esp_app_desc_t image;
    result = esp_ota_get_partition_description(partition, &image);
    if (result != ESP_OK)
    {
        return result;
    }
    if (strncmp(image.project_name, esp_app_get_description()->project_name,
                sizeof(image.project_name)) != 0)
    {
        return ESP_ERR_OTA_VALIDATE_FAILED;
    }
    if (!link_is_current(request))
    {
        return ESP_ERR_INVALID_STATE;
    }

    result = esp_ota_set_boot_partition(partition);
    if (result == ESP_OK)
    {
        ESP_LOGI(TAG, "Boot target set to %s, version %.32s", partition->label, image.version);
    }
    return result;
}

static void handle_command(const ota_request_t *request)
{
    const uint16_t command = read_le16(request->data);
    esp_err_t result = ESP_ERR_INVALID_CRC;
    if (read_le16(request->data + OTA_CRC_OFFSET) == ota_crc16(request->data, OTA_CRC_OFFSET))
    {
        result = command == OTA_START ? start_transfer(request) : finish_transfer(request);
    }

    if (result != ESP_OK)
    {
        ESP_LOGW(TAG, "OTA command %u refused: %s", command, esp_err_to_name(result));
    }
    send_reply(request, OTA_COMMAND_ACK, command, result == ESP_OK ? 0 : OTA_COMMAND_REFUSED);
    if (command == OTA_END && result == ESP_OK)
    {
        esp_restart();
    }
}

static void handle_firmware(const ota_request_t *request)
{
    const uint16_t sector = read_le16(request->data);
    const uint8_t packet = request->data[2];
    const bool last = packet == OTA_LAST_PACKET;
    const size_t payload = request->length - OTA_PACKET_HEADER - (last ? sizeof(uint16_t) : 0);
    uint16_t status = 0;

    if (s_transfer.partition == NULL || s_transfer.written == s_transfer.total)
    {
        status = OTA_SECTOR_LENGTH_ERROR;
    }
    else if (sector != s_transfer.sector_index || (!last && packet != s_transfer.next_packet))
    {
        status = OTA_SECTOR_INDEX_ERROR;
    }
    else
    {
        const size_t remaining = s_transfer.total - s_transfer.written;
        const size_t expected = remaining < sizeof(s_sector) ? remaining : sizeof(s_sector);
        if (payload > expected - s_transfer.sector_used)
        {
            status = OTA_SECTOR_LENGTH_ERROR;
        }
        else
        {
            memcpy(s_sector + s_transfer.sector_used, request->data + OTA_PACKET_HEADER, payload);
            s_transfer.sector_used += payload;
            if (!last)
            {
                ++s_transfer.next_packet;
                return;
            }

            if (s_transfer.sector_used != expected)
            {
                status = OTA_SECTOR_LENGTH_ERROR;
            }
            else if (read_le16(request->data + request->length - sizeof(uint16_t)) !=
                     ota_crc16(s_sector, expected))
            {
                status = OTA_SECTOR_CRC_ERROR;
            }
            else
            {
                esp_err_t result = esp_ota_write(s_transfer.handle, s_sector, expected);
                if (result == ESP_OK)
                {
                    s_transfer.written += expected;
                    ++s_transfer.sector_index;
                }
                else
                {
                    ESP_LOGE(TAG, "OTA flash write failed: %s", esp_err_to_name(result));
                    abort_transfer();
                    status = OTA_SECTOR_LENGTH_ERROR;
                }
            }
        }
    }

    s_transfer.next_packet = 0;
    s_transfer.sector_used = 0;
    send_reply(request, sector, status, s_transfer.sector_index);
}

static void flash_writer(void *argument)
{
    (void)argument;
    while (true)
    {
        ota_request_t request;
        const TickType_t wait = s_transfer.partition != NULL ? pdMS_TO_TICKS(250) : portMAX_DELAY;
        const bool received = xQueueReceive(s_requests, &request, wait) == pdTRUE;
        if (s_transfer.partition != NULL && s_transfer.generation != atomic_load(&s_generation))
        {
            ESP_LOGI(TAG, "OTA connection closed; discarding incomplete transfer");
            abort_transfer();
        }
        if (!received || !link_is_current(&request))
        {
            continue;
        }
        if (request.attr_handle == s_command_handle)
        {
            handle_command(&request);
        }
        else
        {
            handle_firmware(&request);
        }
    }
}

static int ota_access(uint16_t connection, uint16_t attribute, struct ble_gatt_access_ctxt *context,
                      void *argument)
{
    (void)argument;
    if (context->op != BLE_GATT_ACCESS_OP_WRITE_CHR || connection != s_phone)
    {
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }

    ota_request_t request = {
        .generation = atomic_load(&s_generation),
        .conn_handle = connection,
        .attr_handle = attribute,
        .length = OS_MBUF_PKTLEN(context->om),
    };
    if (request.length > sizeof(request.data) ||
        (attribute == s_command_handle ? request.length != OTA_FRAME_SIZE
                                       : request.length <= OTA_PACKET_HEADER))
    {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    if (ble_hs_mbuf_to_flat(context->om, request.data, request.length, NULL) != 0)
    {
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (attribute == s_command_handle)
    {
        const uint16_t command = read_le16(request.data);
        if (command != OTA_START && command != OTA_END)
        {
            return BLE_ATT_ERR_VALUE_NOT_ALLOWED;
        }
    }
    else if (request.data[2] == OTA_LAST_PACKET &&
             request.length <= OTA_PACKET_HEADER + sizeof(uint16_t))
    {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    /* The host returns the ATT response without erasing or writing flash. */
    return xQueueSend(s_requests, &request, 0) == pdTRUE ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static const struct ble_gatt_chr_def OTA_CHARACTERISTICS[] = {
    {
        .uuid = BLE_UUID16_DECLARE(OTA_FIRMWARE_UUID),
        .access_cb = ota_access,
        .val_handle = &s_firmware_handle,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_INDICATE,
    },
    {
        /* The Android client subscribes to all four characteristics before START. */
        .uuid = BLE_UUID16_DECLARE(OTA_PROGRESS_UUID),
        .access_cb = ota_access,
        .flags = BLE_GATT_CHR_F_INDICATE,
    },
    {
        .uuid = BLE_UUID16_DECLARE(OTA_COMMAND_UUID),
        .access_cb = ota_access,
        .val_handle = &s_command_handle,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_INDICATE,
    },
    {
        .uuid = BLE_UUID16_DECLARE(OTA_CUSTOMER_UUID),
        .access_cb = ota_access,
        .flags = BLE_GATT_CHR_F_INDICATE,
    },
    {0},
};

static const struct ble_gatt_svc_def OTA_SERVICES[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(OTA_SERVICE_UUID16),
        .characteristics = OTA_CHARACTERISTICS,
    },
    {0},
};

esp_err_t ota_update_init(void)
{
    if (ble_gatts_count_cfg(OTA_SERVICES) != 0 || ble_gatts_add_svcs(OTA_SERVICES) != 0)
    {
        return ESP_FAIL;
    }

    s_requests = xQueueCreate(4, sizeof(ota_request_t));
    s_ble_ready = xSemaphoreCreateBinary();
    if (s_requests == NULL || s_ble_ready == NULL ||
        xTaskCreate(flash_writer, "ota_writer", 4096, NULL, 5, &s_writer) != pdPASS)
    {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void ota_update_link_reset(void)
{
    s_phone = BLE_HS_CONN_HANDLE_NONE;
    atomic_fetch_add(&s_generation, 1);
    xTaskNotifyGive(s_writer);
}

void ota_update_gap_event(const struct ble_gap_event *event)
{
    switch (event->type)
    {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0)
            {
                s_phone = event->connect.conn_handle;
            }
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            ota_update_link_reset();
            break;
        case BLE_GAP_EVENT_NOTIFY_TX:
            if (event->notify_tx.indication && event->notify_tx.status == BLE_HS_EDONE &&
                event->notify_tx.conn_handle == s_phone &&
                (event->notify_tx.attr_handle == s_command_handle ||
                 event->notify_tx.attr_handle == s_firmware_handle))
            {
                xTaskNotifyGive(s_writer);
            }
            break;
        default:
            break;
    }
}

void ota_update_ble_ready(void)
{
    xSemaphoreGive(s_ble_ready);
}

void ota_update_confirm_boot(bool hardware_ready)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK ||
        state != ESP_OTA_IMG_PENDING_VERIFY)
    {
        return;
    }
    if (!hardware_ready || xSemaphoreTake(s_ble_ready, pdMS_TO_TICKS(5000)) != pdTRUE)
    {
        ESP_LOGE(TAG, "Startup check failed; requesting rollback");
        esp_ota_mark_app_invalid_rollback_and_reboot();
        esp_restart();
    }
    ESP_ERROR_CHECK(esp_ota_mark_app_valid_cancel_rollback());
    ESP_LOGI(TAG, "Confirmed %s, version %.32s", running->label,
             esp_app_get_description()->version);
}
