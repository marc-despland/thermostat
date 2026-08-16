/*
 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: LicenseRef-Included
 *
 * Simulated KETOTEK KTF0177 thermostat - Zigbee end device standing in for
 * the real device during /gateway development/testing. Structural template:
 * esp-idf/examples/zigbee/light_sample/HA_on_off_light (only local end-device
 * example) + the cluster mix from ../../gateway/main/esp_zigbee_gateway.h.
 *
 * This example code is in the Public Domain (or CC0 licensed, at your option.)
 *
 * Unless required by applicable law or agreed to in writing, this
 * software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 */

#include "esp_err.h"
#include "esp_zigbee_core.h"

/* Zigbee configuration */
#define INSTALLCODE_POLICY_ENABLE       false                                /* enable the install code policy for security */
#define ED_AGING_TIMEOUT                ESP_ZB_ED_AGING_TIMEOUT_64MIN        /* aging timeout of device */
#define ED_KEEP_ALIVE                   3000                                 /* 3000 millisecond */
#define THERMOSTAT_SIM_ENDPOINT         1                                    /* mirrors ESP_ZB_GATEWAY_ENDPOINT on /gateway */
#define ESP_ZB_PRIMARY_CHANNEL_MASK     ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK /* end device scans, doesn't fix a channel */

/* Basic manufacturer information - placeholder values; TODO: replace with the
 * real KETOTEK Basic cluster strings once known/needed for interop testing. */
#define ESP_MANUFACTURER_NAME "\x09""ESPRESSIF"      /* Customized manufacturer name */
#define ESP_MODEL_IDENTIFIER "\x07"CONFIG_IDF_TARGET /* Customized model identifier */

/* Default simulated Thermostat cluster values (mirrors /gateway's defaults) */
#define DEFAULT_LOCAL_TEMPERATURE           (2000)  /* Default 20C */
#define DEFAULT_OCCUPIED_HEATING_SETPOINT   (2100)  /* Default 21C */
#define DEFAULT_SYSTEM_MODE                 (0x04)  /* Heat mode */

#define ESP_ZB_ZED_CONFIG()                                         \
    {                                                               \
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ED,                       \
        .install_code_policy = INSTALLCODE_POLICY_ENABLE,           \
        .nwk_cfg.zed_cfg = {                                        \
            .ed_timeout = ED_AGING_TIMEOUT,                         \
            .keep_alive = ED_KEEP_ALIVE,                            \
        },                                                          \
    }

#define ESP_ZB_DEFAULT_RADIO_CONFIG()                           \
    {                                                           \
        .radio_mode = ZB_RADIO_MODE_NATIVE,                     \
    }

#define ESP_ZB_DEFAULT_HOST_CONFIG()                            \
    {                                                           \
        .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE,   \
    }
