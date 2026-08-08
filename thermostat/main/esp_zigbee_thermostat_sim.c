/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: LicenseRef-Included
 *
 * Simulated KETOTEK KTF0177 thermostat - Zigbee end device. Joins /gateway's
 * network via standard BDB steering, exposes the same cluster mix as
 * /gateway (Basic, Identify, Power Config, Thermostat 0x0201, Tuya 0xEF00
 * dual-role), and mimics the real device's autonomous DP reporting so
 * /gateway can be developed/tested without the real hardware.
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_zigbee_thermostat_sim.h"
#include "tuya_ketotek_dp.h"

#if !defined ZB_ED_ROLE
#error Define ZB_ED_ROLE (CONFIG_ZB_ZED=y in sdkconfig.defaults) to compile this end-device source code.
#endif

static const char *TAG = "ESP_ZB_THERMOSTAT_SIM";

/* Simulated local thermostat state. ZCL Thermostat cluster attributes are in
 * 0.01C units (x100); Tuya DP values for KETOTEK are in 0.1C units (x10) -
 * see tuya_dp_from_zcl_temp()/tuya_dp_to_zcl_temp() below for the conversion. */
static int16_t g_local_temperature = DEFAULT_LOCAL_TEMPERATURE;
static int16_t g_occupied_heating_setpoint = DEFAULT_OCCUPIED_HEATING_SETPOINT;
static uint8_t g_system_mode = DEFAULT_SYSTEM_MODE;

/* Coordinator (== /gateway) short address is always 0x0000 in a simple
 * network where this end device joins directly to it (true for the two-board
 * dev/test setup this project targets). */
#define GATEWAY_SHORT_ADDR 0x0000

/* Endpoint on /gateway's side that owns the Tuya cluster instances - matches
 * ESP_ZB_GATEWAY_ENDPOINT in ../../gateway/main/esp_zigbee_gateway.h (both
 * projects use endpoint 1). */
#define GATEWAY_TUYA_ENDPOINT 1

static uint16_t g_tuya_seq = 0;
static esp_timer_handle_t g_report_timer = NULL;

/* x100 (ZCL) <-> x10 (Tuya DP) temperature unit conversion. */
static inline int32_t tuya_dp_from_zcl_temp(int16_t zcl_x100)
{
    return zcl_x100 / 10;
}
static inline int16_t tuya_dp_to_zcl_temp(int32_t tuya_x10)
{
    return (int16_t)(tuya_x10 * 10);
}

static void tuya_send_dp_value(uint16_t dst_addr, uint8_t dst_endpoint, uint8_t dp_id, int32_t value)
{
    uint8_t buf[10];
    g_tuya_seq++;
    if (g_tuya_seq >= 0xFFFF) {
        g_tuya_seq = 0;
    }
    buf[0] = (g_tuya_seq >> 8) & 0xFF;
    buf[1] = g_tuya_seq & 0xFF;
    buf[2] = dp_id;
    buf[3] = TUYA_DP_TYPE_VALUE;
    buf[4] = 0x00;
    buf[5] = 0x04;
    tuya_dp_encode_value(&buf[6], value);

    esp_zb_zcl_custom_cluster_cmd_req_t cmd_req = {
        .zcl_basic_cmd = {
            .dst_addr_u.addr_short = dst_addr,
            .dst_endpoint = dst_endpoint,
            .src_endpoint = THERMOSTAT_SIM_ENDPOINT,
        },
        .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        .cluster_id = TUYA_CLUSTER_ID,
        .custom_cmd_id = TUYA_CMD_REPORT_1,
        /* Mirror of /gateway's tuya_send_set_temperature(), reversed: gateway
         * receives DP reports on its Tuya CLIENT-role cluster instance, so
         * this end device's SERVER-role instance sends with direction
         * TO_CLI. TODO: verify against a real two-board join (see plan's
         * verification step 4) - not yet exercised on real hardware. */
        .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI,
        .data = {
            .type = ESP_ZB_ZCL_ATTR_TYPE_ARRAY,
            .value = buf,
            .size = sizeof(buf),
        },
    };
    uint8_t tx_seq = esp_zb_zcl_custom_cluster_cmd_req(&cmd_req);
    if (tx_seq == 0xFF) {
        ESP_LOGE(TAG, "Failed to send DP%u report: stack rejected command", dp_id);
    } else {
        ESP_LOGI(TAG, "Sent DP%u report: value=%d (tx_seq=0x%02x)", dp_id, (int)value, tx_seq);
    }
}

static void tuya_send_dp_bool(uint16_t dst_addr, uint8_t dst_endpoint, uint8_t dp_id, bool value)
{
    uint8_t buf[7];
    g_tuya_seq++;
    if (g_tuya_seq >= 0xFFFF) {
        g_tuya_seq = 0;
    }
    buf[0] = (g_tuya_seq >> 8) & 0xFF;
    buf[1] = g_tuya_seq & 0xFF;
    buf[2] = dp_id;
    buf[3] = TUYA_DP_TYPE_BOOL;
    buf[4] = 0x00;
    buf[5] = 0x01;
    buf[6] = value ? 1 : 0;

    esp_zb_zcl_custom_cluster_cmd_req_t cmd_req = {
        .zcl_basic_cmd = {
            .dst_addr_u.addr_short = dst_addr,
            .dst_endpoint = dst_endpoint,
            .src_endpoint = THERMOSTAT_SIM_ENDPOINT,
        },
        .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        .cluster_id = TUYA_CLUSTER_ID,
        .custom_cmd_id = TUYA_CMD_REPORT_1,
        .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_CLI,
        .data = {
            .type = ESP_ZB_ZCL_ATTR_TYPE_ARRAY,
            .value = buf,
            .size = sizeof(buf),
        },
    };
    uint8_t tx_seq = esp_zb_zcl_custom_cluster_cmd_req(&cmd_req);
    if (tx_seq == 0xFF) {
        ESP_LOGE(TAG, "Failed to send DP%u report: stack rejected command", dp_id);
    } else {
        ESP_LOGI(TAG, "Sent DP%u report: value=%s (tx_seq=0x%02x)", dp_id, value ? "true" : "false", tx_seq);
    }
}

/* Report the handful of DPs relevant to "read the thermostat's data"
 * (SPECIFICATIONS.md) - system state, local temperature, current setpoint.
 * Real Tuya devices send one DP per message, so this issues several sends. */
static void tuya_send_dp_report(uint16_t dst_addr, uint8_t dst_endpoint)
{
    tuya_send_dp_bool(dst_addr, dst_endpoint, KETOTEK_DP_SYSTEM_STATE, g_system_mode != 0);
    tuya_send_dp_value(dst_addr, dst_endpoint, KETOTEK_DP_LOCAL_TEMP, tuya_dp_from_zcl_temp(g_local_temperature));
    tuya_send_dp_value(dst_addr, dst_endpoint, KETOTEK_DP_HEATING_SETPOINT, tuya_dp_from_zcl_temp(g_occupied_heating_setpoint));
}

/* esp_timer callbacks run outside the Zigbee stack's own task - hop onto the
 * Zigbee scheduler (like the stack's own main loop) before touching any
 * esp_zb_* API, same pattern /gateway's old timer hack skipped by mistake. */
static void tuya_send_dp_report_alarm_cb(uint8_t param)
{
    (void)param;
    if (esp_zb_bdb_dev_joined()) {
        tuya_send_dp_report(GATEWAY_SHORT_ADDR, GATEWAY_TUYA_ENDPOINT);
    }
}

static void report_timer_callback(void *arg)
{
    esp_zb_scheduler_alarm((esp_zb_callback_t)tuya_send_dp_report_alarm_cb, 0, 0);
}

static void start_report_timer(void)
{
    if (g_report_timer == NULL) {
        const esp_timer_create_args_t timer_args = {
            .callback = &report_timer_callback,
            .name = "tuya_dp_report_timer",
        };
        esp_timer_create(&timer_args, &g_report_timer);
    }
    esp_timer_start_periodic(g_report_timer, (uint64_t)CONFIG_THERMOSTAT_SIM_REPORT_INTERVAL_SEC * 1000000ULL);
    ESP_LOGI(TAG, "Started periodic DP report timer (every %d s)", CONFIG_THERMOSTAT_SIM_REPORT_INTERVAL_SEC);
}

static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    ESP_RETURN_ON_FALSE(esp_zb_bdb_start_top_level_commissioning(mode_mask) == ESP_OK, , TAG, "Failed to start Zigbee commissioning");
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;

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
                /* End device: search for an open network to join - NOT
                 * MODE_NETWORK_FORMATION, that's the coordinator-only path
                 * used by /gateway. */
                ESP_LOGI(TAG, "Start network steering");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            } else {
                ESP_LOGI(TAG, "Device rebooted");
                start_report_timer();
            }
        } else {
            ESP_LOGW(TAG, "Failed to initialize Zigbee stack (status: %s)", esp_err_to_name(err_status));
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            esp_zb_ieee_addr_t extended_pan_id;
            esp_zb_get_extended_pan_id(extended_pan_id);
            ESP_LOGI(TAG, "Joined network successfully (Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, PAN ID: 0x%04hx, Channel:%d, Short Address: 0x%04hx)",
                     extended_pan_id[7], extended_pan_id[6], extended_pan_id[5], extended_pan_id[4],
                     extended_pan_id[3], extended_pan_id[2], extended_pan_id[1], extended_pan_id[0],
                     esp_zb_get_pan_id(), esp_zb_get_current_channel(), esp_zb_get_short_address());
            start_report_timer();
        } else {
            ESP_LOGI(TAG, "Network steering was not successful (status: %s)", esp_err_to_name(err_status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;
    default:
        ESP_LOGI(TAG, "ZDO signal: %s (0x%x), status: %s", esp_zb_zdo_signal_to_string(sig_type), sig_type,
                 esp_err_to_name(err_status));
        break;
    }
}

/* Standard ZCL Thermostat cluster (0x0201) attribute writes, e.g. from a
 * generic ZCL controller - kept for symmetry with /gateway's standard-cluster
 * path, alongside the Tuya path below which is how the real KETOTEK actually
 * gets reprogrammed. */
static esp_err_t zb_attribute_handler(const esp_zb_zcl_set_attr_value_message_t *message)
{
    ESP_RETURN_ON_FALSE(message, ESP_FAIL, TAG, "Empty message");
    ESP_RETURN_ON_FALSE(message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG, TAG, "Received message: error status(%d)",
                        message->info.status);

    if (message->info.dst_endpoint == THERMOSTAT_SIM_ENDPOINT && message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_THERMOSTAT) {
        if (message->attribute.id == ESP_ZB_ZCL_ATTR_THERMOSTAT_OCCUPIED_HEATING_SETPOINT_ID) {
            g_occupied_heating_setpoint = message->attribute.data.value ? *(int16_t *)message->attribute.data.value : g_occupied_heating_setpoint;
            ESP_LOGI(TAG, "Heating setpoint set (ZCL) to %d.%02d C", g_occupied_heating_setpoint / 100, g_occupied_heating_setpoint % 100);
        } else if (message->attribute.id == ESP_ZB_ZCL_ATTR_THERMOSTAT_SYSTEM_MODE_ID) {
            g_system_mode = message->attribute.data.value ? *(uint8_t *)message->attribute.data.value : g_system_mode;
            ESP_LOGI(TAG, "System mode set (ZCL) to %d", g_system_mode);
        }
    }
    return ESP_OK;
}

/* Decode a Tuya "Set DataPoint" command (cmd 0x00) from /gateway and apply it
 * to the simulated local state - this is the "reprogramming" path
 * (SPECIFICATIONS.md), mirroring /gateway's DP103 table. */
static void handle_tuya_set_data(const uint8_t *data, uint16_t len)
{
    if (len < TUYA_DP_HEADER_LEN) {
        return;
    }
    uint8_t dp_id = data[2];
    uint8_t dp_type = data[3];
    uint16_t dp_len = (data[4] << 8) | data[5];
    if (len < TUYA_DP_HEADER_LEN + dp_len) {
        ESP_LOGW(TAG, "Truncated Set DataPoint payload: dp_id=%u announced_len=%u total_len=%u", dp_id, dp_len, len);
        return;
    }
    const uint8_t *dp_data = &data[TUYA_DP_HEADER_LEN];

    switch (dp_id) {
    case KETOTEK_DP_HEATING_SETPOINT:
        if (dp_len == 4) {
            int32_t tuya_setpoint = tuya_dp_decode_value(dp_data);
            g_occupied_heating_setpoint = tuya_dp_to_zcl_temp(tuya_setpoint);
            ESP_LOGI(TAG, "DP%u: heating setpoint updated to %.1f C", dp_id, tuya_setpoint / 10.0);
        }
        break;
    case KETOTEK_DP_SYSTEM_STATE:
        if (dp_len >= 1) {
            g_system_mode = dp_data[0] ? DEFAULT_SYSTEM_MODE : 0x00;
            ESP_LOGI(TAG, "DP%u: system state updated to %s", dp_id, dp_data[0] ? "ON" : "OFF");
        }
        break;
    default:
        ESP_LOGI(TAG, "DP%u: type=%u len=%u (received, not applied to simulated state)", dp_id, dp_type, dp_len);
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
    case ESP_ZB_CORE_CMD_CUSTOM_CLUSTER_REQ_CB_ID: {
        esp_zb_zcl_custom_cluster_command_message_t *custom_cmd = (esp_zb_zcl_custom_cluster_command_message_t *)message;
        uint8_t cmd_id = custom_cmd->info.command.id;
        uint8_t *data = custom_cmd->data.value;
        uint16_t len = custom_cmd->data.size;
        ESP_LOGI(TAG, "TUYA custom command from gateway - Cmd:0x%02x, DataLen:%d", cmd_id, len);

        if (cmd_id == TUYA_CMD_SET_DATA && data && len > 0) {
            handle_tuya_set_data(data, len);
        } else if (cmd_id == TUYA_CMD_QUERY) {
            ESP_LOGI(TAG, "Data Query received - sending DP report");
            tuya_send_dp_report(GATEWAY_SHORT_ADDR, GATEWAY_TUYA_ENDPOINT);
        }
        break;
    }
    default:
        ESP_LOGW(TAG, "Receive Zigbee action(0x%x) callback", callback_id);
        break;
    }
    return ret;
}

static void esp_zb_task(void *pvParameters)
{
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZED_CONFIG();
    esp_zb_init(&zb_nwk_cfg);
    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);

    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();
    esp_zb_endpoint_config_t endpoint_config = {
        .endpoint = THERMOSTAT_SIM_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_THERMOSTAT_DEVICE_ID,
        .app_device_version = 0,
    };

    /* Basic Cluster */
    esp_zb_attribute_list_t *basic_cluster = esp_zb_basic_cluster_create(NULL);
    esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, ESP_MANUFACTURER_NAME);
    esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, ESP_MODEL_IDENTIFIER);
    esp_zb_cluster_list_add_basic_cluster(cluster_list, basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* Identify Cluster */
    esp_zb_cluster_list_add_identify_cluster(cluster_list, esp_zb_identify_cluster_create(NULL), ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* Power Configuration Cluster - KETOTEK is battery powered */
    esp_zb_attribute_list_t *power_cluster = esp_zb_power_config_cluster_create(NULL);
    esp_zb_cluster_list_add_power_config_cluster(cluster_list, power_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* Thermostat Cluster (standard ZCL 0x0201) */
    esp_zb_thermostat_cluster_cfg_t thermostat_cfg = {
        .local_temperature = DEFAULT_LOCAL_TEMPERATURE,
        .occupied_heating_setpoint = DEFAULT_OCCUPIED_HEATING_SETPOINT,
    };
    esp_zb_attribute_list_t *thermostat_cluster = esp_zb_thermostat_cluster_create(&thermostat_cfg);
    esp_zb_cluster_list_add_thermostat_cluster(cluster_list, thermostat_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    /* TUYA Custom Cluster 0xef00 - mirrors /gateway's dual-role setup so its
     * existing send/receive plumbing works against this simulator unmodified. */
    esp_zb_attribute_list_t *tuya_cluster_client = esp_zb_zcl_attr_list_create(TUYA_CLUSTER_ID);
    esp_zb_cluster_list_add_custom_cluster(cluster_list, tuya_cluster_client, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);
    esp_zb_attribute_list_t *tuya_cluster_server = esp_zb_zcl_attr_list_create(TUYA_CLUSTER_ID);
    esp_zb_cluster_list_add_custom_cluster(cluster_list, tuya_cluster_server, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

    esp_zb_ep_list_add_ep(ep_list, cluster_list, endpoint_config);

    esp_zb_device_register(ep_list);
    esp_zb_core_action_handler_register(zb_action_handler);
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();
}

void app_main(void)
{
    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));
    xTaskCreate(esp_zb_task, "Zigbee_main", 8192, NULL, 5, NULL);
}
