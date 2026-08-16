/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: LicenseRef-Included
 *
 * Zigbee Gateway - adapted from the ESP-IDF esp_zigbee_gateway example and
 * from Thermostat/old/main/esp_zigbee_gateway.h.
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#include "esp_err.h"
#include "esp_zigbee_core.h"

/* Zigbee Configuration */
#define MAX_CHILDREN                    CONFIG_THERMOSTAT_MAX_CHILDREN
#define INSTALLCODE_POLICY_ENABLE       false                              /* enable the install code policy for security */
#define ESP_ZB_PRIMARY_CHANNEL_MASK     (1l << CONFIG_THERMOSTAT_DEFAULT_CHANNEL)
#define ESP_ZB_GATEWAY_ENDPOINT         1                                  /* Gateway endpoint identifier */
#define APP_PROD_CFG_CURRENT_VERSION    0x0001                             /* Production configuration version */

/* Basic manufacturer information */
#define ESP_MANUFACTURER_CODE 0x131B                 /* Customized manufacturer code */
#define ESP_MANUFACTURER_NAME "\x09""ESPRESSIF"      /* Customized manufacturer name */
#define ESP_MODEL_IDENTIFIER "\x07"CONFIG_IDF_TARGET /* Customized model identifier */

/* Thermostat Cluster Attributes */
#define ESP_TEMP_SENSOR_UPDATE_INTERVAL     (1)     /* Local temperature update interval (seconds) */
#define ESP_TEMP_SENSOR_MIN_VALUE           (500)   /* Min temperature in 0.01C (5C) */
#define ESP_TEMP_SENSOR_MAX_VALUE           (3000)  /* Max temperature in 0.01C (30C) */
#define ESP_TEMP_SENSOR_TOLERANCE           (100)   /* Temperature tolerance in 0.01C (1C) */

/* Default Thermostat Values */
#define DEFAULT_LOCAL_TEMPERATURE           (2000)  /* Default 20C */
#define DEFAULT_OCCUPIED_HEATING_SETPOINT   (2100)  /* Default 21C */
#define DEFAULT_MIN_HEAT_SETPOINT_LIMIT     (500)   /* 5C */
#define DEFAULT_MAX_HEAT_SETPOINT_LIMIT     (3000)  /* 30C */
#define DEFAULT_CONTROL_SEQUENCE            (0x04)  /* Heating only */
#define DEFAULT_SYSTEM_MODE                 (0x04)  /* Heat mode */

#define ESP_ZB_ZC_CONFIG()                                                              \
    {                                                                                   \
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_COORDINATOR,                                  \
        .install_code_policy = INSTALLCODE_POLICY_ENABLE,                               \
        .nwk_cfg.zczr_cfg = {                                                           \
            .max_children = MAX_CHILDREN,                                               \
        },                                                                              \
    }

/* Both /gateway and /thermostat use the ESP32-C6's native 802.15.4 radio -
 * no external RCP co-processor, so only the ZB_RADIO_NATIVE path is kept. */
#define ESP_ZB_DEFAULT_RADIO_CONFIG()                           \
    {                                                           \
        .radio_mode = ZB_RADIO_MODE_NATIVE,                     \
    }

#define ESP_ZB_DEFAULT_HOST_CONFIG()                            \
    {                                                           \
        .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE,   \
    }
