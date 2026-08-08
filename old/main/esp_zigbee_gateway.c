/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: LicenseRef-Included
 *
 * Zigbee Gateway Example
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */
#include <fcntl.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#include "esp_coexist.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_vfs_dev.h"
#include "esp_vfs_usb_serial_jtag.h"
#include "esp_vfs_eventfd.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "protocol_examples_common.h"
#include "esp_zigbee_gateway.h"
#include "zb_config_platform.h"

static const char *TAG = "ESP_ZB_GATEWAY";

/* Global variables for thermostat state */
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

/* Timer for periodic temperature commands */
static esp_timer_handle_t g_set_temp_timer = NULL;
static uint16_t g_new_device_addr = 0;

/* Track report frequency from thermostat */
static uint64_t g_last_report_time = 0;

/* Sequence number management - synchronized with device */
static uint16_t g_tuya_seq = 0;           // Our sequence counter for commands
static uint16_t g_last_device_seq = 0;    // Last seq number received from device

/* Pending command queue - send when device wakes up */
static bool g_pending_setpoint_cmd = false;
static bool g_send_in_progress = false;  // Track if command send is in progress
static bool g_schedule_command_send = false;  // Flag to trigger scheduled send
static uint8_t g_tuya_cmd_buffer[10];   // Static buffer for TUYA commands

/* Forward declarations */
static void tuya_send_set_temperature(uint16_t dst_addr, uint8_t dst_endpoint, int16_t temperature_deg);
static void zigbee_send_command_callback(uint8_t param);

/* Thermostat attribute update callback */
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
                ESP_LOGI(TAG, "Thermostat heating setpoint changed to %d.%02d°C", 
                         g_occupied_heating_setpoint / 100, g_occupied_heating_setpoint % 100);
            } else if (message->attribute.id == ESP_ZB_ZCL_ATTR_THERMOSTAT_SYSTEM_MODE_ID) {
                g_system_mode = message->attribute.data.value ? *(uint8_t *)message->attribute.data.value : DEFAULT_SYSTEM_MODE;
                ESP_LOGI(TAG, "Thermostat system mode changed to %d", g_system_mode);
            } else if (message->attribute.id == ESP_ZB_ZCL_ATTR_THERMOSTAT_LOCAL_TEMPERATURE_ID) {
                g_local_temperature = message->attribute.data.value ? *(int16_t *)message->attribute.data.value : DEFAULT_LOCAL_TEMPERATURE;
                ESP_LOGI(TAG, "Local temperature updated to %d.%02d°C", 
                         g_local_temperature / 100, g_local_temperature % 100);
            }
        } else if (message->info.cluster == 0xef00) {
            /* KETOTEK custom cluster (Tuya proprietary) - acknowledge silently */
            ESP_LOGD(TAG, "KETOTEK custom cluster 0xef00 message received (ignored)");
        }
    }
    return ret;
}

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    esp_err_t ret = ESP_OK;
    switch (callback_id) {
    case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
        ret = zb_attribute_handler((esp_zb_zcl_set_attr_value_message_t *)message);
        break;
    case ESP_ZB_CORE_CMD_CUSTOM_CLUSTER_REQ_CB_ID: {
        /* Handle TUYA custom cluster 0xef00 commands from KETOTEK */
        esp_zb_zcl_custom_cluster_command_message_t *custom_cmd = (esp_zb_zcl_custom_cluster_command_message_t *)message;
        ESP_LOGI(TAG, "TUYA Custom command - Cluster:0x%04x, Cmd:0x%02x, Addr:0x%04x, Endpoint:%d, DataLen:%d", 
                 custom_cmd->info.cluster, custom_cmd->info.command.id, 
                 custom_cmd->info.src_address.u.short_addr, custom_cmd->info.src_endpoint, custom_cmd->data.size);
        
        /* Display payload */
        if (custom_cmd->data.size > 0 && custom_cmd->data.value) {
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, custom_cmd->data.value, custom_cmd->data.size, ESP_LOG_INFO);
            
            /* Track report frequency */
            uint64_t current_time_us = esp_timer_get_time();
            if (g_last_report_time > 0) {
                uint64_t interval_us = current_time_us - g_last_report_time;
                uint32_t interval_ms = interval_us / 1000;
                uint32_t interval_sec = interval_ms / 1000;
                uint32_t interval_ms_remainder = interval_ms % 1000;
                ESP_LOGI(TAG, "📊 Report interval: %u.%03us (%u ms) from device 0x%04x", 
                         interval_sec, interval_ms_remainder, interval_ms,
                         custom_cmd->info.src_address.u.short_addr);
            }
            g_last_report_time = current_time_us;
            
            uint8_t cmd_id = custom_cmd->info.command.id;
            uint8_t *data = custom_cmd->data.value;
            uint16_t len = custom_cmd->data.size;
            
            /* FIRST: Extract sequence number from TUYA message (first 2 bytes) */
            if (len >= 2) {
                g_last_device_seq = (data[0] << 8) | data[1];
                ESP_LOGI(TAG, "📩 Device seq: 0x%04x (%u)", g_last_device_seq, g_last_device_seq);
            }
            
            /* THEN: Device is awake - send pending command NOW (using extracted sequence) */
            if (custom_cmd->info.src_address.u.short_addr == g_new_device_addr) {
                /* If we have a pending command, send it now while device is awake */
                if (g_pending_setpoint_cmd && !g_send_in_progress) {
                    ESP_LOGI(TAG, "✅ Device 0x%04x awake - sending PENDING command NOW (using device seq)", g_new_device_addr);
                    tuya_send_set_temperature(g_new_device_addr, 1, 10);
                    g_pending_setpoint_cmd = false;
                }
                
                /* Restart periodic timer to queue next command in 20 seconds */
                if (g_set_temp_timer != NULL) {
                    esp_err_t stop_err = esp_timer_stop(g_set_temp_timer);
                    if (stop_err == ESP_OK || stop_err == ESP_ERR_INVALID_STATE) {
                        esp_err_t start_err = esp_timer_start_periodic(g_set_temp_timer, 20000000);
                        if (start_err == ESP_OK) {
                            ESP_LOGI(TAG, "▶️  Periodic timer restarted (20s)");
                        }
                    }
                }
            }
            
            /* Decode TUYA DataPoint reports (cmd 0x01 or 0x02) */
            if ((cmd_id == 0x01 || cmd_id == 0x02) && len >= 6) {
                uint8_t dp_id = data[2];        // DataPoint ID
                uint8_t dp_type = data[3];      // Type: 01=bool, 02=value/int
                uint16_t dp_len = (data[4] << 8) | data[5];  // Length
                
                if (len >= 6 + dp_len) {
                    /* Decode common KETOTEK DataPoints */
                    if (dp_id == 0x02 && dp_type == 0x04 && dp_len == 4) {
                        int32_t temp = (data[6] << 24) | (data[7] << 16) | (data[8] << 8) | data[9];
                        ESP_LOGI(TAG, "  DP02: Current Temperature = %.1f°C", temp / 10.0);
                    } else if (dp_id == 0x04 && dp_type == 0x02 && dp_len == 4) {
                        int32_t setpoint = (data[6] << 24) | (data[7] << 16) | (data[8] << 8) | data[9];
                        ESP_LOGI(TAG, "  DP04: Temperature Setpoint = %.1f°C", setpoint / 10.0);
                    } else if (dp_id == 0x05 && dp_type == 0x02 && dp_len == 4) {
                        int32_t local_temp = (data[6] << 24) | (data[7] << 16) | (data[8] << 8) | data[9];
                        ESP_LOGI(TAG, "  DP05: Local Temperature = %.1f°C", local_temp / 10.0);
                    } else if (dp_id == 0x03 && dp_type == 0x04 && dp_len == 1) {
                        uint8_t mode = data[6];
                        ESP_LOGI(TAG, "  DP03: Mode = %s", mode ? "Manual" : "Schedule");
                    } else if (dp_id == 0x04 && dp_type == 0x04 && dp_len == 1) {
                        uint8_t state = data[6];
                        ESP_LOGI(TAG, "  DP04: Heating State = %s", state == 2 ? "ON" : (state == 1 ? "IDLE" : "OFF"));
                    } else if (dp_id == 0x07 && dp_type == 0x01 && dp_len == 1) {
                        uint8_t child_lock = data[6];
                        ESP_LOGI(TAG, "  DP07: Child Lock = %s", child_lock ? "ON" : "OFF");
                    } else if (dp_id >= 0x1c && dp_id <= 0x22 && dp_len == 25) {
                        ESP_LOGI(TAG, "  DP%02X: Weekly Schedule Day %d", dp_id, dp_id - 0x1b);
                    } else {
                        ESP_LOGI(TAG, "  DP%02X: Type=%d, Len=%d", dp_id, dp_type, dp_len);
                    }
                }
            } else if (cmd_id == 0x24) {
                ESP_LOGI(TAG, "  → TUYA Time Sync Request (seq=%d)", data[0]);
            } else if (cmd_id == 0x11) {
                ESP_LOGI(TAG, "  → TUYA Data Query Response");
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

/* Send TUYA DataQuery command to query device status */
static void tuya_send_data_query(uint16_t dst_addr, uint8_t dst_endpoint)
{
    /* Increment sequence number (global counter) */
    g_tuya_seq++;
    if (g_tuya_seq >= 0xFFFF) {
        g_tuya_seq = 0;
    }
    
    /* TUYA DataQuery format for cmd 0x11: [seq_H, seq_L, 0x00] */
    uint8_t tuya_query[] = {
        (g_tuya_seq >> 8) & 0xFF,   // Sequence High
        g_tuya_seq & 0xFF,          // Sequence Low
        0x00                        // Query all DataPoints (0x00 = query all)
    };
    
    ESP_LOGI(TAG, "📊 Sending TUYA DataQuery: Request all DP values (seq=0x%04x/%u) to device 0x%04x", 
             g_tuya_seq, g_tuya_seq, dst_addr);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, tuya_query, sizeof(tuya_query), ESP_LOG_INFO);
    
    /* Verify the stack is ready */
    if (!esp_zb_bdb_dev_joined()) {
        ESP_LOGE(TAG, "❌ Cannot send command: Gateway not joined to network");
        return;
    }
    
    /* Build ZCL command with direction CLIENT_TO_SERVER */
    esp_zb_zcl_custom_cluster_cmd_req_t cmd_req = {
        .zcl_basic_cmd = {
            .dst_addr_u.addr_short = dst_addr,
            .dst_endpoint = dst_endpoint,
            .src_endpoint = ESP_ZB_GATEWAY_ENDPOINT,
        },
        .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        .cluster_id = 0xef00,       // TUYA custom cluster
        .custom_cmd_id = 0x11,      // Command 0x11: Query DataPoints
        .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_SRV,
        .data = {
            .type = ESP_ZB_ZCL_ATTR_TYPE_ARRAY,
            .value = tuya_query,
            .size = sizeof(tuya_query),
        },
    };
    
    /* Send command - returns transaction sequence number (0xFF = failure) */
    uint8_t tx_seq = esp_zb_zcl_custom_cluster_cmd_req(&cmd_req);
    if (tx_seq == 0xFF) {
        ESP_LOGE(TAG, "❌ Failed to send DataQuery: stack rejected command");
        ESP_LOGW(TAG, "⚠️  Device 0x%04x appears to be sleeping or unavailable", dst_addr);
    } else {
        ESP_LOGI(TAG, "✅ DataQuery sent successfully (tx_seq=0x%02x)", tx_seq);
    }
}

/* Send TUYA command to set temperature setpoint */
static void tuya_send_set_temperature(uint16_t dst_addr, uint8_t dst_endpoint, int16_t temperature_deg)
{
    /* Use device's last seq + 1, or increment our counter if no device seq yet */
    uint16_t cmd_seq;
    if (g_last_device_seq > 0) {
        /* We have received messages from device - use its seq + 1 */
        cmd_seq = g_last_device_seq + 1;
        ESP_LOGI(TAG, "📌 Using device seq+1: 0x%04x (device was 0x%04x)", cmd_seq, g_last_device_seq);
    } else {
        /* No message from device yet - use our counter */
        g_tuya_seq++;
        if (g_tuya_seq >= 0xFFFF) {
            g_tuya_seq = 0;
        }
        cmd_seq = g_tuya_seq;
        ESP_LOGI(TAG, "📌 Using our seq counter: 0x%04x", cmd_seq);
    }
    
    /* Build command directly in static buffer (persistent memory for Zigbee API) */
    /* TUYA DataPoint format for cmd 0x00: [seq_H, seq_L, DP_ID, DP_TYPE, LEN_H, LEN_L, ...DATA] */
    g_tuya_cmd_buffer[0] = (cmd_seq >> 8) & 0xFF;      // Sequence High
    g_tuya_cmd_buffer[1] = cmd_seq & 0xFF;             // Sequence Low
    g_tuya_cmd_buffer[2] = 0x04;                       // DP_ID: 0x04 = Temperature Setpoint (KETOTEK uses DP04, not DP103)
    g_tuya_cmd_buffer[3] = 0x02;                       // DP_TYPE: 0x02 = Value (4 bytes integer)
    g_tuya_cmd_buffer[4] = 0x00;                       // DP_LENGTH High
    g_tuya_cmd_buffer[5] = 0x04;                       // DP_LENGTH Low (4 bytes)
    
    /* Convert temperature to Tuya format (temperature * 10) */
    int32_t tuya_temp = temperature_deg * 10;
    g_tuya_cmd_buffer[6] = (tuya_temp >> 24) & 0xFF;
    g_tuya_cmd_buffer[7] = (tuya_temp >> 16) & 0xFF;
    g_tuya_cmd_buffer[8] = (tuya_temp >> 8) & 0xFF;
    g_tuya_cmd_buffer[9] = tuya_temp & 0xFF;
    
    ESP_LOGI(TAG, "📤 Sending TUYA Setpoint: %d°C (DP04/0x04) to device 0x%04x", temperature_deg, dst_addr);
    ESP_LOGI(TAG, "   📌 Sequence: our_seq_counter=0x%04x, device_last_seq=0x%04x, sending_seq=0x%04x", g_tuya_seq, g_last_device_seq, cmd_seq);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, g_tuya_cmd_buffer, 10, ESP_LOG_INFO);
    
    /* Verify the stack is ready and device is in routing table */
    if (!esp_zb_bdb_dev_joined()) {
        ESP_LOGE(TAG, "❌ Cannot send: Gateway not joined to network");
        return;
    }
    
    ESP_LOGD(TAG, "   ✓ Gateway joined, endpoint=%d, cluster=0xef00", dst_endpoint);
    
    /* Prevent concurrent sends (static buffer prevents reuse) */
    if (g_send_in_progress) {
        ESP_LOGW(TAG, "⚠️  Send already in progress, skipping duplicate");
        return;
    }
    
    /* Build ZCL command with direction TO_SRV (from gateway/server to device/client) */
    esp_zb_zcl_custom_cluster_cmd_req_t cmd_req = {
        .zcl_basic_cmd = {
            .dst_addr_u.addr_short = dst_addr,
            .dst_endpoint = dst_endpoint,
            .src_endpoint = ESP_ZB_GATEWAY_ENDPOINT,
        },
        .address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        .cluster_id = 0xef00,       // TUYA custom cluster
        .custom_cmd_id = 0x00,      // Command 0x00: Set DataPoint
        .direction = ESP_ZB_ZCL_CMD_DIRECTION_TO_SRV,  // From SERVER (us) to CLIENT (device)
        .data = {
            .type = ESP_ZB_ZCL_ATTR_TYPE_ARRAY,
            .value = g_tuya_cmd_buffer,  // Use static buffer - NOT stack local!
            .size = 10,
        },
    };
    
    /* Send command - returns transaction sequence number (0xFF = failure) */
    uint8_t tx_seq = esp_zb_zcl_custom_cluster_cmd_req(&cmd_req);
    
    /* Reset flag after send attempt */
    g_send_in_progress = false;

    if (tx_seq == 0xFF) {
        ESP_LOGE(TAG, "❌ FAILED to send: Command rejected by stack (seq=0x%02x)", tx_seq);
        ESP_LOGE(TAG, "   Device 0x%04x may not be in routing table or stack exhausted", dst_addr);
        ESP_LOGE(TAG, "   Sequence number was: 0x%04x (incremented from last attempt)", cmd_seq);
        /* Stop timer on repeated errors */
        if (g_set_temp_timer != NULL) {
            esp_timer_stop(g_set_temp_timer);
            ESP_LOGI(TAG, "⏸️  Stopped periodic commands");
        }
    } else {
        ESP_LOGI(TAG, "✅ TUYA command sent successfully (tx_seq=0x%02x, our_seq=0x%04x)", tx_seq, cmd_seq);
        ESP_LOGI(TAG, "   Device seq sync: was 0x%04x → now 0x%04x", g_last_device_seq, cmd_seq);
        /* Update last device seq to the one we just used */
        g_last_device_seq = cmd_seq;
    }
}

/* Zigbee scheduler callback to send pending command */
static void zigbee_send_command_callback(uint8_t param)
{
    /* Just mark that we should send - don't call Zigbee API directly from scheduler */
    if (g_schedule_command_send && g_pending_setpoint_cmd && g_new_device_addr != 0) {
        ESP_LOGI(TAG, "⏰ Scheduler: FLAGGING command for send (will process in app handler)");
        g_send_in_progress = false;  // Ready to send
        /* Actual send will happen from next event handler cycle */
    }
}

/* Timer callback to mark setpoint as pending when device wakes up */
static void set_temp_timer_callback(void *arg)
{
    if (g_new_device_addr == 0) {
        ESP_LOGW(TAG, "⏰ Timer tick - no device paired yet, skipping");
        return;
    }
    
    ESP_LOGI(TAG, "⏰ Timer tick: marked setpoint 10°C as PENDING for device 0x%04x", g_new_device_addr);
    g_pending_setpoint_cmd = true;
    /* Command will be sent 3s after device wakes up and sends a report */
}


/* Note: Please select the correct console output port based on the development board in menuconfig */
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
esp_err_t esp_zb_gateway_console_init(void)
{
    esp_err_t ret = ESP_OK;
    /* Disable buffering on stdin */
    setvbuf(stdin, NULL, _IONBF, 0);

    /* Minicom, screen, idf_monitor send CR when ENTER key is pressed */
    usb_serial_jtag_vfs_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
    /* Move the caret to the beginning of the next line on '\n' */
    usb_serial_jtag_vfs_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);

    /* Enable non-blocking mode on stdin and stdout */
    fcntl(fileno(stdout), F_SETFL, O_NONBLOCK);
    fcntl(fileno(stdin), F_SETFL, O_NONBLOCK);

    usb_serial_jtag_driver_config_t usb_serial_jtag_config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    ret = usb_serial_jtag_driver_install(&usb_serial_jtag_config);
    usb_serial_jtag_vfs_use_driver();
    uart_vfs_dev_register();
    return ret;
}
#endif

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
#if CONFIG_EXAMPLE_CONNECT_WIFI
        /* WiFi is OPTIONAL - only if configured */
        #if CONFIG_ESP_COEX_SW_COEXIST_ENABLE
        esp_coex_wifi_i154_enable();
        #endif /* CONFIG_ESP_COEX_SW_COEXIST_ENABLE */
        if (example_connect() != ESP_OK) {
            ESP_LOGW(TAG, "WiFi connection failed - continuing with Zigbee only");
        } else {
            ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
        }
#else
        /* WiFi disabled - use Zigbee only (recommended) */
        ESP_LOGI(TAG, "WiFi disabled - using Zigbee only");
#endif /* CONFIG_EXAMPLE_CONNECT_WIFI */
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
            
            /* Auto-open network for 180 seconds to allow device pairing */
            ESP_LOGI(TAG, "Opening network for 180 seconds - Press KETOTEK pairing button now!");
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
        ESP_LOGI(TAG, "✅ Device FOUND! New device commissioned or rejoined (short: 0x%04hx)", dev_annce_params->device_short_addr);
        
        /* Register the device */
        if (g_paired_devices_count < MAX_PAIRED_DEVICES) {
            g_paired_devices[g_paired_devices_count].short_addr = dev_annce_params->device_short_addr;
            g_paired_devices[g_paired_devices_count].endpoint = 1;
            snprintf(g_paired_devices[g_paired_devices_count].model, sizeof(g_paired_devices[g_paired_devices_count].model), "Device_%d", g_paired_devices_count + 1);
            g_paired_devices_count++;
            ESP_LOGI(TAG, "✅ Device REGISTERED! Total paired devices: %d", g_paired_devices_count);
            
            /* List all devices */
            ESP_LOGI(TAG, "=== PAIRED DEVICES ===");
            for (int i = 0; i < g_paired_devices_count; i++) {
                ESP_LOGI(TAG, "Device %d: Short Address=0x%04hx", i + 1, g_paired_devices[i].short_addr);
            }
            ESP_LOGI(TAG, "=====================");
            
            /* Schedule temperature command 5 seconds after pairing */
            g_new_device_addr = dev_annce_params->device_short_addr;
            ESP_LOGI(TAG, "⏱️  Starting periodic temperature commands (every 20 seconds)...");
            
            if (g_set_temp_timer == NULL) {
                const esp_timer_create_args_t timer_args = {
                    .callback = &set_temp_timer_callback,
                    .name = "set_temp_timer"
                };
                esp_timer_create(&timer_args, &g_set_temp_timer);
            }
            esp_timer_start_periodic(g_set_temp_timer, 20000000);  // 20 seconds in microseconds
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
        ESP_LOGW(TAG, "🚫 Device became unavailable (sleeping/offline)");
        /* Stop the periodic timer when device goes to sleep */
        if (g_set_temp_timer != NULL) {
            esp_timer_stop(g_set_temp_timer);
            ESP_LOGI(TAG, "⏸️  Stopped periodic commands - device is sleeping");
        }
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
    
    /* Power Configuration Cluster - version simplifiée */
    esp_zb_attribute_list_t *power_cluster = esp_zb_power_config_cluster_create(NULL);
    esp_zb_cluster_list_add_power_config_cluster(cluster_list, power_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    
    /* Thermostat Cluster */
    esp_zb_thermostat_cluster_cfg_t thermostat_cfg = {
        .local_temperature = DEFAULT_LOCAL_TEMPERATURE,
        .occupied_heating_setpoint = DEFAULT_OCCUPIED_HEATING_SETPOINT,
    };
    esp_zb_attribute_list_t *thermostat_cluster = esp_zb_thermostat_cluster_create(&thermostat_cfg);
    esp_zb_cluster_list_add_thermostat_cluster(cluster_list, thermostat_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    
    /* TUYA Custom Cluster 0xef00 - Support KETOTEK proprietary commands */
    ESP_LOGI(TAG, "Adding TUYA custom cluster 0xef00 for KETOTEK support (CLIENT + SERVER roles)");
    
    /* Add as CLIENT to receive commands/reports from KETOTEK */
    esp_zb_attribute_list_t *tuya_cluster_client = esp_zb_zcl_attr_list_create(0xef00);
    esp_zb_cluster_list_add_custom_cluster(cluster_list, tuya_cluster_client, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);
    
    /* Add as SERVER to send commands to KETOTEK */
    esp_zb_attribute_list_t *tuya_cluster_server = esp_zb_zcl_attr_list_create(0xef00);
    esp_zb_cluster_list_add_custom_cluster(cluster_list, tuya_cluster_server, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    
    esp_zb_ep_list_add_gateway_ep(ep_list, cluster_list, endpoint_config);
    
    /* Register action callback */
    esp_zb_core_action_handler_register(zb_action_handler);
    esp_zb_device_register(ep_list);
    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();
    vTaskDelete(NULL);
}

void app_main(void)
{
    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };

    ESP_ERROR_CHECK(esp_zb_platform_config(&config));
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    ESP_ERROR_CHECK(esp_zb_gateway_console_init());
#endif
    xTaskCreate(esp_zb_task, "Zigbee_main", 8192, NULL, 5, NULL);
}
