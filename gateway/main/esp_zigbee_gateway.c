/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: LicenseRef-Included
 *
 * Zigbee Gateway - adapted from the ESP-IDF esp_zigbee_gateway example and
 * from Thermostat/old/main/esp_zigbee_gateway.c, targeting the KETOTEK
 * KTF0177 thermostatic valve. See SPECIFICATIONS.md and
 * old/ZIGBEE2MQTT_ANALYSIS.md for protocol background.
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_zigbee_gateway.h"
#include "tuya_ketotek_dp.h"

static const char *TAG = "ESP_ZB_GATEWAY";

/* Clearly-tagged markers for the "Cas simple" scenario steps (SPECIFICATIONS.md),
 * logged at WARN level so they stand out from the regular INFO noise when
 * watching two idf.py monitor windows side by side (gateway + thermostat). */
#define SCENARIO_LOG(fmt, ...) ESP_LOGW(TAG, "########## CAS SIMPLE [GATEWAY] - " fmt " ##########", ##__VA_ARGS__)

/* Global variables for thermostat state, mirrored from ZCL Thermostat cluster
 * attribute reports (standard cluster 0x0201 path). */
static int16_t g_local_temperature = DEFAULT_LOCAL_TEMPERATURE;
static int16_t g_occupied_heating_setpoint = DEFAULT_OCCUPIED_HEATING_SETPOINT;
static uint8_t g_system_mode = DEFAULT_SYSTEM_MODE;

/* Device list for tracking paired devices */
typedef struct {
    uint16_t short_addr;
    uint8_t endpoint;
    char model[64];
} zb_device_t;

#define MAX_PAIRED_DEVICES 10
static zb_device_t g_paired_devices[MAX_PAIRED_DEVICES];
static uint8_t g_paired_devices_count = 0;

/* Sequence number management - synchronized with device */
static uint16_t g_tuya_seq = 0;        /* Our sequence counter for commands */
static uint16_t g_last_device_seq = 0; /* Last seq number received from device */

/* Static buffer for outgoing TUYA commands - the Zigbee API needs the buffer
 * to stay valid for the duration of the async send, so it can't be a stack
 * local. Single in-flight send at a time (see g_send_in_progress). */
static uint8_t g_tuya_cmd_buffer[10];
static bool g_send_in_progress = false;

/* Track report interval from the paired device, for diagnostics only. */
static uint64_t g_last_report_time = 0;

/* "Cas simple" test scenario (SPECIFICATIONS.md): send a demo setpoint once,
 * right after the FIRST Tuya status report received since THIS gateway boot -
 * not on a free-running timer (see the removed old/ hack). Deliberately NOT
 * gated on ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE: that signal only fires on a true
 * (re)association, but a device that's already joined and just reboots
 * (esp_zb_bdb_is_factory_new() == false, e.g. the real KETOTEK, or our own
 * thermostat sim across repeated test reflashes) resumes silently without
 * re-announcing - relying on DEVICE_ANNCE left g_awaiting_setpoint_addr at 0
 * forever after the gateway's own reboot, in which case a report could
 * arrive and be logged but the demo setpoint would never fire. Use the
 * report's own source address instead. */
static bool g_demo_setpoint_sent = false;
#define DEMO_HEATING_SETPOINT_DEG 21

static void tuya_send_set_temperature(uint16_t dst_addr, uint8_t dst_endpoint, int16_t temperature_deg);

/* Standard ZCL Thermostat cluster (0x0201) attribute update callback. */
static esp_err_t zb_attribute_handler(const esp_zb_zcl_set_attr_value_message_t *message)
{
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(message, ESP_FAIL, TAG, "Empty message");
    ESP_RETURN_ON_FALSE(message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG, TAG, "Received message: error status(%d)",
                        message->info.status);
    ESP_LOGI(TAG, "Received message: endpoint(%d), cluster(0x%x), attribute(0x%x), data size(%d)", message->info.dst_endpoint, message->info.cluster,
             message->attribute.id, message->attribute.data.size);

    if (message->info.dst_endpoint == ESP_ZB_GATEWAY_ENDPOINT) {
        if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT) {
            if (message->attribute.id == ESP_ZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_HEATING_SETPOINT_ID) {
                g_occupied_heating_setpoint = message->attribute.data.value ? *(int16_t *)message->attribute.data.value : DEFAULT_OCCUPIED_HEATING_SETPOINT;
                ESP_LOGI(TAG, "Thermostat heating setpoint changed to %d.%02d C",
                         g_occupied_heating_setpoint / 100, g_occupied_heating_setpoint % 100);
            } else if (message->attribute.id == ESP_ZB_ZCL_ATTR_THERMOSTAT_SYSTEM_MODE_ID) {
                g_system_mode = message->attribute.data.value ? *(uint8_t *)message->attribute.data.value : DEFAULT_SYSTEM_MODE;
                ESP_LOGI(TAG, "Thermostat system mode changed to %d", g_system_mode);
            } else if (message->attribute.id == ESP_ZB_ZCL_ATTR_THERMOSTAT_LOCAL_TEMPERATURE_ID) {
                g_local_temperature = message->attribute.data.value ? *(int16_t *)message->attribute.data.value : DEFAULT_LOCAL_TEMPERATURE;
                ESP_LOGI(TAG, "Local temperature updated to %d.%02d C",
                         g_local_temperature / 100, g_local_temperature % 100);
            }
        } else if (message->info.cluster == TUYA_CLUSTER_ID) {
            /* Attribute reports on the Tuya cluster aren't used by KETOTEK - it
             * talks over custom cluster commands instead, handled in
             * zb_action_handler()/ESP_ZB_CORE_CMD_CUSTOM_CLUSTER_REQ_CB_ID. */
            ESP_LOGD(TAG, "Tuya custom cluster 0x%04x attribute message received (ignored)", TUYA_CLUSTER_ID);
        }
    }
    return ret;
}

/* Decode a single Tuya DataPoint from a 0xEF00 report/query-response payload
 * and log it. `dp_data` points at the DP's value bytes (after the 6-byte
 * seq/dp_id/dp_type/len header), `dp_len` is the announced value length. */
static void tuya_log_dp(uint8_t dp_id, uint8_t dp_type, uint16_t dp_len, const uint8_t *dp_data)
{
    switch (dp_id) {
    case KETOTEK_DP_HEATING_STATE:
        ESP_LOGI(TAG, "  DP%u (HeatingState/enum): %u", dp_id, dp_len >= 1 ? dp_data[0] : 0);
        break;
    case KETOTEK_DP_WINDOW_DETECTION:
        ESP_LOGI(TAG, "  DP%u (WindowDetection/bool): %s", dp_id, (dp_len >= 1 && dp_data[0]) ? "ON" : "OFF");
        break;
    case KETOTEK_DP_FROST_DETECTION:
        ESP_LOGI(TAG, "  DP%u (FrostDetection/bool): %s", dp_id, (dp_len >= 1 && dp_data[0]) ? "ON" : "OFF");
        break;
    case KETOTEK_DP_TEMP_CALIBRATION:
        if (dp_len == 4) {
            ESP_LOGI(TAG, "  DP%u (TempCalibration/value): %d", dp_id, (int)tuya_dp_decode_value(dp_data));
        } else if (dp_len >= 1) {
            ESP_LOGI(TAG, "  DP%u (TempCalibration/value, 1B): %d", dp_id, (int8_t)dp_data[0]);
        }
        break;
    case KETOTEK_DP_CHILD_LOCK:
        ESP_LOGI(TAG, "  DP%u (ChildLock/bool): %s", dp_id, (dp_len >= 1 && dp_data[0]) ? "ON" : "OFF");
        break;
    case KETOTEK_DP_SYSTEM_STATE:
        ESP_LOGI(TAG, "  DP%u (SystemState/bool): %s", dp_id, (dp_len >= 1 && dp_data[0]) ? "ON" : "OFF");
        break;
    case KETOTEK_DP_LOCAL_TEMP:
        if (dp_len == 4) {
            ESP_LOGI(TAG, "  DP%u (LocalTemp/value): %.1f C", dp_id, tuya_dp_decode_value(dp_data) / 10.0);
        }
        break;
    case KETOTEK_DP_HEATING_SETPOINT:
        if (dp_len == 4) {
            ESP_LOGI(TAG, "  DP%u (HeatingSetpoint/value): %.1f C", dp_id, tuya_dp_decode_value(dp_data) / 10.0);
        }
        break;
    case KETOTEK_DP_VALVE_POSITION:
        if (dp_len == 4) {
            ESP_LOGI(TAG, "  DP%u (ValvePosition/value): %d%%", dp_id, (int)tuya_dp_decode_value(dp_data));
        }
        break;
    case KETOTEK_DP_BATTERY_LOW:
        ESP_LOGI(TAG, "  DP%u (BatteryLow/bool): %s", dp_id, (dp_len >= 1 && dp_data[0]) ? "LOW" : "OK");
        break;
    case KETOTEK_DP_AWAY_MODE:
        ESP_LOGI(TAG, "  DP%u (AwayMode/bool): %s", dp_id, (dp_len >= 1 && dp_data[0]) ? "ON" : "OFF");
        break;
    case KETOTEK_DP_SCHEDULE_MODE:
        ESP_LOGI(TAG, "  DP%u (ScheduleMode/enum): %u", dp_id, dp_len >= 1 ? dp_data[0] : 0);
        break;
    case KETOTEK_DP_SCHEDULE_ENABLE:
        ESP_LOGI(TAG, "  DP%u (ScheduleEnable/bool): %s", dp_id, (dp_len >= 1 && dp_data[0]) ? "ON" : "OFF");
        break;
    default:
        if (dp_id == KETOTEK_DP_WEEKLY_SCHEDULE_BASE || (dp_id >= 123 && dp_id <= 129)) {
            /* Weekly schedule DP - raw bytes only for this pass, no decode/write support yet. */
            ESP_LOGI(TAG, "  DP%u (WeeklySchedule): %u raw bytes (not decoded)", dp_id, dp_len);
        } else {
            ESP_LOGI(TAG, "  DP%u: type=%u len=%u (unhandled)", dp_id, dp_type, dp_len);
        }
        break;
    }
}

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    esp_err_t ret = ESP_OK;
    switch (callback_id) {
    case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
        ret = zb_attribute_handler((esp_zb_zcl_set_attr_value_message_t *)message);
        break;
    case ESP_ZB_CORE_CMD_CUSTOM_CLUSTER_REQ_CB_ID:
    case ESP_ZB_CORE_CMD_CUSTOM_CLUSTER_RESP_CB_ID: {
        /* Handle TUYA custom cluster 0xef00 commands from KETOTEK. Both
         * REQ and RESP fire here: the SDK dispatches direction=TO_SRV
         * frames to _REQ_CB_ID and direction=TO_CLI frames to
         * _RESP_CB_ID - Tuya's report/command flow doesn't map cleanly to
         * that req/resp split, so handle both identically. */
        esp_zb_zcl_custom_cluster_command_message_t *custom_cmd = (esp_zb_zcl_custom_cluster_command_message_t *)message;
        ESP_LOGI(TAG, "TUYA custom command - Cluster:0x%04x, Cmd:0x%02x, Addr:0x%04x, Endpoint:%d, DataLen:%d",
                 custom_cmd->info.cluster, custom_cmd->info.command.id,
                 custom_cmd->info.src_address.u.short_addr, custom_cmd->info.src_endpoint, custom_cmd->data.size);

        if (custom_cmd->data.size > 0 && custom_cmd->data.value) {
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, custom_cmd->data.value, custom_cmd->data.size, ESP_LOG_INFO);

            /* Track report interval, diagnostics only. */
            uint64_t current_time_us = esp_timer_get_time();
            if (g_last_report_time > 0) {
                uint64_t interval_ms = (current_time_us - g_last_report_time) / 1000;
                ESP_LOGI(TAG, "Report interval: %llu.%03llus from device 0x%04x",
                         interval_ms / 1000, interval_ms % 1000, custom_cmd->info.src_address.u.short_addr);
            }
            g_last_report_time = current_time_us;

            uint8_t cmd_id = custom_cmd->info.command.id;
            uint8_t *data = custom_cmd->data.value;
            uint16_t len = custom_cmd->data.size;

            /* Extract sequence number from TUYA message (first 2 bytes) so
             * outgoing commands can chain off of it (see tuya_send_set_temperature). */
            if (len >= 2) {
                g_last_device_seq = (data[0] << 8) | data[1];
                ESP_LOGI(TAG, "Device seq: 0x%04x (%u)", g_last_device_seq, g_last_device_seq);
            }

            if ((cmd_id == TUYA_CMD_REPORT_1 || cmd_id == TUYA_CMD_REPORT_2) && len >= TUYA_DP_HEADER_LEN) {
                uint8_t dp_id = data[2];
                uint8_t dp_type = data[3];
                uint16_t dp_len = (data[4] << 8) | data[5];
                if (len >= TUYA_DP_HEADER_LEN + dp_len) {
                    tuya_log_dp(dp_id, dp_type, dp_len, &data[TUYA_DP_HEADER_LEN]);
                } else {
                    ESP_LOGW(TAG, "Truncated DP payload: dp_id=%u announced_len=%u total_len=%u", dp_id, dp_len, len);
                }

                /* "Cas simple" scenario, step "La gateway envoi la temperature
                 * de consigne au thermostat": react to the first status
                 * report seen since boot by sending the demo setpoint back
                 * to whoever sent it (one-shot per gateway boot, see
                 * g_demo_setpoint_sent above - not gated on DEVICE_ANNCE). */
                if (!g_demo_setpoint_sent) {
                    uint16_t src_addr = custom_cmd->info.src_address.u.short_addr;
                    uint8_t src_ep = custom_cmd->info.src_endpoint;
                    SCENARIO_LOG("Etape 4/5: STATUT RECU du thermostat (device=0x%04x)", src_addr);
                    SCENARIO_LOG("Etape 5/5: ENVOI DE LA CONSIGNE au thermostat (%d C) - voir 'TUYA command sent' ci-dessous pour le resultat", DEMO_HEATING_SETPOINT_DEG);
                    tuya_send_set_temperature(src_addr, src_ep, DEMO_HEATING_SETPOINT_DEG);
                    g_demo_setpoint_sent = true;
                }
            } else if (cmd_id == TUYA_CMD_TIME_SYNC) {
                ESP_LOGI(TAG, "  -> TUYA Time Sync Request (TODO: unhandled, no response sent)");
            } else if (cmd_id == TUYA_CMD_QUERY) {
                ESP_LOGI(TAG, "  -> TUYA Data Query Response");
            }
        }
        ret = ESP_OK;
        break;
    }
    default:
        ESP_LOGW(TAG, "Receive Zigbee action(0x%x) callback", callback_id);
        break;
    }
    return ret;
}

/* Send TUYA DataQuery command to query device status (cmd 0x11: query all DPs). */
static void tuya_send_data_query(uint16_t dst_addr, uint8_t dst_endpoint)
{
    g_tuya_seq++;
    if (g_tuya_seq >= 0xFFFF) {
        g_tuya_seq = 0;
    }

    uint8_t tuya_query[] = {
        (g_tuya_seq >> 8) & 0xFF,
        g_tuya_seq & 0xFF,
        0x00, /* query all DataPoints */
    };

    ESP_LOGI(TAG, "Sending TUYA DataQuery (seq=0x%04x) to device 0x%04x", g_tuya_seq, dst_addr);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, tuya_query, sizeof(tuya_query), ESP_LOG_INFO);

    if (!esp_zb_bdb_dev_joined()) {
        ESP_LOGE(TAG, "Cannot send command: gateway not joined to network");
        return;
    }

    esp_zb_zcl_custom_cluster_cmd_req_t cmd_req = {
        .zcl_basic_cmd = {
            .dst_addr_u.addr_short = dst_addr,
            .dst_endpoint = dst_endpoint,
            .src_endpoint = ESP_ZB_GATEWAY_ENDPOINT,
        },
        .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        .profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .cluster_id = TUYA_CLUSTER_ID,
        .custom_cmd_id = TUYA_CMD_QUERY,
        .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_SRV,
        .data = {
            /* NOT ARRAY: per the SDK doc, ARRAY/16BIT_ARRAY/32BIT_ARRAY expect
             * the first 2 bytes of the buffer to BE a count prefix that the
             * stack re-parses (size = 2 + sum of content len) - our raw Tuya
             * bytes (seq number first) got misread as a huge count, inflating
             * the computed frame size until NWK silently dropped it as
             * "too big" (confirmed via a low-level APS trace capture on
             * real hardware: buf_len 261/517/773 for 7-10 byte payloads).
             * SET has no such prefix - size is just the raw byte count. */
            .type = ESP_ZB_ZCL_ATTR_TYPE_SET,
            .value = tuya_query,
            .size = sizeof(tuya_query),
        },
    };

    uint8_t tx_seq = esp_zb_zcl_custom_cluster_cmd_req(&cmd_req);
    if (tx_seq == 0xFF) {
        ESP_LOGE(TAG, "Failed to send DataQuery: stack rejected command");
    } else {
        ESP_LOGI(TAG, "DataQuery sent successfully (tx_seq=0x%02x)", tx_seq);
    }
}

/* Send TUYA "Set DataPoint" command to write the heating setpoint (DP103).
 *
 * NOTE: this is the send-side plumbing only - it is not wired to any
 * automatic trigger in this pass (the old code's "resend 10C every 20s"
 * timer hack has been intentionally removed). Call this manually (e.g. from
 * a debugger, or temporarily from ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE below) to
 * smoke-test the DP103 remap against /thermostat. A real "set setpoint to X"
 * entry point (console CLI or similar) is a follow-up, see
 * CONFIG_THERMOSTAT_ENABLE_CLI. */
static void tuya_send_set_temperature(uint16_t dst_addr, uint8_t dst_endpoint, int16_t temperature_deg)
{
    uint16_t cmd_seq;
    if (g_last_device_seq > 0) {
        cmd_seq = g_last_device_seq + 1;
    } else {
        g_tuya_seq++;
        if (g_tuya_seq >= 0xFFFF) {
            g_tuya_seq = 0;
        }
        cmd_seq = g_tuya_seq;
    }

    g_tuya_cmd_buffer[0] = (cmd_seq >> 8) & 0xFF;
    g_tuya_cmd_buffer[1] = cmd_seq & 0xFF;
    g_tuya_cmd_buffer[2] = KETOTEK_DP_HEATING_SETPOINT; /* DP103 */
    g_tuya_cmd_buffer[3] = TUYA_DP_TYPE_VALUE;
    g_tuya_cmd_buffer[4] = 0x00;
    g_tuya_cmd_buffer[5] = 0x04; /* 4-byte value */
    tuya_dp_encode_value(&g_tuya_cmd_buffer[6], (int32_t)temperature_deg * 10);

    ESP_LOGI(TAG, "Sending TUYA Setpoint: %d C (DP%u) to device 0x%04x, seq=0x%04x",
             temperature_deg, KETOTEK_DP_HEATING_SETPOINT, dst_addr, cmd_seq);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, g_tuya_cmd_buffer, 10, ESP_LOG_INFO);

    if (!esp_zb_bdb_dev_joined()) {
        ESP_LOGE(TAG, "Cannot send: gateway not joined to network");
        return;
    }
    if (g_send_in_progress) {
        ESP_LOGW(TAG, "Send already in progress, skipping duplicate");
        return;
    }

    esp_zb_zcl_custom_cluster_cmd_req_t cmd_req = {
        .zcl_basic_cmd = {
            .dst_addr_u.addr_short = dst_addr,
            .dst_endpoint = dst_endpoint,
            .src_endpoint = ESP_ZB_GATEWAY_ENDPOINT,
        },
        .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        .profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .cluster_id = TUYA_CLUSTER_ID,
        .custom_cmd_id = TUYA_CMD_SET_DATA,
        .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_SRV,
        .data = {
            /* NOT ARRAY: per the SDK doc, ARRAY/16BIT_ARRAY/32BIT_ARRAY expect
             * the first 2 bytes of the buffer to BE a count prefix that the
             * stack re-parses (size = 2 + sum of content len) - our raw Tuya
             * bytes (seq number first) got misread as a huge count, inflating
             * the computed frame size until NWK silently dropped it as
             * "too big" (confirmed via a low-level APS trace capture on
             * real hardware: buf_len 261/517/773 for 7-10 byte payloads).
             * SET has no such prefix - size is just the raw byte count. */
            .type = ESP_ZB_ZCL_ATTR_TYPE_SET,
            .value = g_tuya_cmd_buffer,
            .size = 10,
        },
    };

    uint8_t tx_seq = esp_zb_zcl_custom_cluster_cmd_req(&cmd_req);
    g_send_in_progress = false;

    if (tx_seq == 0xFF) {
        ESP_LOGE(TAG, "FAILED to send: command rejected by stack (device 0x%04x may not be routable)", dst_addr);
    } else {
        ESP_LOGI(TAG, "TUYA command sent successfully (tx_seq=0x%02x)", tx_seq);
        g_last_device_seq = cmd_seq;
    }
}

static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    ESP_RETURN_ON_FALSE(esp_zb_bdb_start_top_level_commissioning(mode_mask) == ESP_OK, , TAG, "Failed to start Zigbee bdb commissioning");
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p       = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;
    esp_zb_zdo_signal_device_annce_params_t *dev_annce_params = NULL;

    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Initialize Zigbee stack");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "Device started up in %s factory-reset mode", esp_zb_bdb_is_factory_new() ? "" : "non");
            if (esp_zb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "Start network formation");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_FORMATION);
            } else {
                esp_zb_bdb_open_network(180);
                ESP_LOGI(TAG, "Device rebooted");
            }
        } else {
            ESP_LOGE(TAG, "Failed to initialize Zigbee stack (status: %s)", esp_err_to_name(err_status));
        }
        break;
    case ESP_ZB_BDB_SIGNAL_FORMATION:
        if (err_status == ESP_OK) {
            esp_zb_ieee_addr_t ieee_address;
            esp_zb_get_long_address(ieee_address);
            ESP_LOGI(TAG, "Formed network successfully (Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, PAN ID: 0x%04hx, Channel:%d, Short Address: 0x%04hx)",
                     ieee_address[7], ieee_address[6], ieee_address[5], ieee_address[4],
                     ieee_address[3], ieee_address[2], ieee_address[1], ieee_address[0],
                     esp_zb_get_pan_id(), esp_zb_get_current_channel(), esp_zb_get_short_address());

            SCENARIO_LOG("Gateway PRETE - reseau ouvert 180s, en attente du thermostat");
            ESP_LOGI(TAG, "Opening network for 180 seconds - start KETOTEK pairing now");
            esp_zb_bdb_open_network(180);

            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        } else {
            ESP_LOGI(TAG, "Restart network formation (status: %s)", esp_err_to_name(err_status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, ESP_ZB_BDB_MODE_NETWORK_FORMATION, 1000);
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "Network steering started");
        }
        break;
    case ESP_ZB_ZDO_SIGNAL_DEVICE_ANNCE:
        dev_annce_params = (esp_zb_zdo_signal_device_annce_params_t *)esp_zb_app_signal_get_params(p_sg_p);
        ESP_LOGI(TAG, "Device FOUND! New device commissioned or rejoined (short: 0x%04hx)", dev_annce_params->device_short_addr);

        if (g_paired_devices_count < MAX_PAIRED_DEVICES) {
            g_paired_devices[g_paired_devices_count].short_addr = dev_annce_params->device_short_addr;
            g_paired_devices[g_paired_devices_count].endpoint = 1;
            snprintf(g_paired_devices[g_paired_devices_count].model, sizeof(g_paired_devices[g_paired_devices_count].model), "Device_%d", g_paired_devices_count + 1);
            g_paired_devices_count++;
            SCENARIO_LOG("Etape 3/5: THERMOSTAT APPAIRE (short=0x%04hx)", dev_annce_params->device_short_addr);
            ESP_LOGI(TAG, "Device REGISTERED! Total paired devices: %d", g_paired_devices_count);

            ESP_LOGI(TAG, "=== PAIRED DEVICES ===");
            for (int i = 0; i < g_paired_devices_count; i++) {
                ESP_LOGI(TAG, "Device %d: Short Address=0x%04hx", i + 1, g_paired_devices[i].short_addr);
            }
            ESP_LOGI(TAG, "======================");

            /* "Cas simple" scenario: don't send the setpoint yet - wait for
             * the first status report seen since boot (see the
             * TUYA_CMD_REPORT handling above, g_demo_setpoint_sent) so
             * whoever sent it is confirmed awake and ready to receive
             * commands. This fires on true (re)association; a device that's
             * already joined and merely reboots won't hit this case at all,
             * but will still trigger the demo setpoint via its first report.
             * TODO(THERMOSTAT_ENABLE_CLI): replace this demo one-shot with a
             * real "set setpoint" console command. */
            ESP_LOGI(TAG, "Will send demo setpoint (%d C) once any device reports its status", DEMO_HEATING_SETPOINT_DEG);
        } else {
            ESP_LOGW(TAG, "Max devices reached (%d). Cannot register more.", MAX_PAIRED_DEVICES);
        }
        break;
    case ESP_ZB_NWK_SIGNAL_PERMIT_JOIN_STATUS:
        if (err_status == ESP_OK) {
            if (*(uint8_t *)esp_zb_app_signal_get_params(p_sg_p)) {
                ESP_LOGI(TAG, "Network(0x%04hx) is open for %d seconds", esp_zb_get_pan_id(), *(uint8_t *)esp_zb_app_signal_get_params(p_sg_p));
            } else {
                ESP_LOGW(TAG, "Network(0x%04hx) closed, devices joining not allowed.", esp_zb_get_pan_id());
            }
        }
        break;
    case ESP_ZB_ZDO_SIGNAL_PRODUCTION_CONFIG_READY:
        ESP_LOGI(TAG, "Production configuration is %s", err_status == ESP_OK ? "ready" : "not present");
        esp_zb_set_node_descriptor_manufacturer_code(ESP_MANUFACTURER_CODE);
        break;
    case ESP_ZB_ZDO_DEVICE_UNAVAILABLE:
        ESP_LOGW(TAG, "Device became unavailable (sleeping/offline)");
        break;
    default:
        ESP_LOGI(TAG, "ZDO signal: %s (0x%x), status: %s", esp_zb_zdo_signal_to_string(sig_type), sig_type,
                 esp_err_to_name(err_status));
        break;
    }
}

static void esp_zb_task(void *pvParameters)
{
    /* initialize Zigbee stack */
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZC_CONFIG();
    esp_zb_init(&zb_nwk_cfg);
    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();
    esp_zb_endpoint_config_t endpoint_config = {
        .endpoint = ESP_ZB_GATEWAY_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_THERMOSTAT_DEVICE_ID,
        .app_device_version = 0,
    };

    /* Basic Cluster */
    esp_zb_attribute_list_t *basic_cluster = esp_zb_basic_cluster_create(NULL);
    esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, ESP_MANUFACTURER_NAME);
    esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, ESP_MODEL_IDENTIFIER);
    uint8_t app_version = APP_PROD_CFG_CURRENT_VERSION;
    esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_APPLICATION_VERSION_ID, &app_version);
    esp_zb_cluster_list_add_basic_cluster(cluster_list, basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* Identify Cluster */
    esp_zb_cluster_list_add_identify_cluster(cluster_list, esp_zb_identify_cluster_create(NULL), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* Power Configuration Cluster */
    esp_zb_attribute_list_t *power_cluster = esp_zb_power_config_cluster_create(NULL);
    esp_zb_cluster_list_add_power_config_cluster(cluster_list, power_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* Thermostat Cluster (standard ZCL 0x0201) */
    esp_zb_thermostat_cluster_cfg_t thermostat_cfg = {
        .local_temperature = DEFAULT_LOCAL_TEMPERATURE,
        .occupied_heating_setpoint = DEFAULT_OCCUPIED_HEATING_SETPOINT,
    };
    esp_zb_attribute_list_t *thermostat_cluster = esp_zb_thermostat_cluster_create(&thermostat_cfg);
    esp_zb_cluster_list_add_thermostat_cluster(cluster_list, thermostat_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* TUYA Custom Cluster 0xef00 - KETOTEK proprietary DP protocol */
    ESP_LOGI(TAG, "Adding TUYA custom cluster 0x%04x (CLIENT + SERVER roles)", TUYA_CLUSTER_ID);
    esp_zb_attribute_list_t *tuya_cluster_client = esp_zb_zcl_attr_list_create(TUYA_CLUSTER_ID);
    esp_zb_cluster_list_add_custom_cluster(cluster_list, tuya_cluster_client, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);
    esp_zb_attribute_list_t *tuya_cluster_server = esp_zb_zcl_attr_list_create(TUYA_CLUSTER_ID);
    esp_zb_cluster_list_add_custom_cluster(cluster_list, tuya_cluster_server, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    esp_zb_ep_list_add_gateway_ep(ep_list, cluster_list, endpoint_config);

    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_device_register(ep_list);
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();
    vTaskDelete(NULL);
}

void app_main(void)
{
    SCENARIO_LOG("Etape 1/5: GATEWAY DEMARRE");
    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };

    ESP_ERROR_CHECK(esp_zb_platform_config(&config));
    ESP_ERROR_CHECK(nvs_flash_init());
    /* TODO(THERMOSTAT_ENABLE_CLI): initialize a console (USB-JTAG VFS) and
     * register permit_join/list_devices/remove_device commands here. */
    xTaskCreate(esp_zb_task, "Zigbee_main", 8192, NULL, 5, NULL);
}
