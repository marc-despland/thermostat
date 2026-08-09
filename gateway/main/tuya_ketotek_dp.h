/*
 * Tuya cluster (0xEF00) protocol constants and KETOTEK KTF0177 DataPoint (DP)
 * table, shared between /gateway and /thermostat.
 *
 * Keep in sync with the identical copy in ../../thermostat/main/tuya_ketotek_dp.h.
 * See SPECIFICATIONS.md and old/ZIGBEE2MQTT_ANALYSIS.md for the origin of this
 * DP103-based table (reverse-engineered from the real KETOTEK KTF0177 by the
 * zigbee-herdsman-converters Saswell converter - NOT the old/ DP04 guess).
 *
 * Wire format for cluster 0xEF00 custom commands:
 *   [SEQ_H][SEQ_L][DP_ID][DP_TYPE][LEN_H][LEN_L][DATA...]
 * Temperatures are x10 encoded (e.g. 21.5 C -> 215), big-endian.
 */
#pragma once

#include <stdint.h>

#define TUYA_CLUSTER_ID          0xEF00

/* Tuya cluster 0xEF00 command IDs */
#define TUYA_CMD_SET_DATA        0x00 /* Set DataPoint (gateway -> device) */
#define TUYA_CMD_REPORT_1        0x01 /* Device -> gateway report */
#define TUYA_CMD_REPORT_2        0x02 /* Device -> gateway report */
#define TUYA_CMD_QUERY           0x11 /* Data Query request (query all DPs) */
#define TUYA_CMD_TIME_SYNC       0x24 /* Time sync request - TODO: unhandled */

typedef enum {
    TUYA_DP_TYPE_BOOL   = 0x01,
    TUYA_DP_TYPE_VALUE  = 0x02, /* int32, big-endian */
    TUYA_DP_TYPE_STRING = 0x03,
    TUYA_DP_TYPE_ENUM   = 0x04,
    TUYA_DP_TYPE_BITMAP = 0x05,
} tuya_dp_type_t;

/* KETOTEK KTF0177 DataPoints (DP103 scheme, per zigbee-herdsman-converters
 * Saswell/KETOTEK - see old/ZIGBEE2MQTT_ANALYSIS.md) */
#define KETOTEK_DP_CONTROL_MODE          2   /* enum, 1 byte - not in the Saswell table,
                                               * identified on real hardware: middle
                                               * button mode toggle, 0=Automatique,
                                               * 1=Manuel (see SPECIFICATIONS.md
                                               * "Protocole") */
#define KETOTEK_DP_HEATING_STATE        3   /* enum */
#define KETOTEK_DP_HEATING_SETPOINT_ECHO 4   /* value, x10 C - not in the Saswell
                                               * table, identified on real hardware:
                                               * mirrors the setpoint currently applied
                                               * on the head, including manual changes
                                               * made on the device itself (see
                                               * SPECIFICATIONS.md "Protocole") */
#define KETOTEK_DP_CHILD_LOCK_REAL      7   /* bool - not in the Saswell table
                                              * (which uses DP40, never observed on
                                              * this device); confirmed on real
                                              * hardware by toggling the child lock
                                              * and observing this DP flip ON/OFF.
                                              * See SPECIFICATIONS.md "Protocole". */
#define KETOTEK_DP_WINDOW_DETECTION     8   /* bool */
#define KETOTEK_DP_FROST_DETECTION      10  /* bool */
#define KETOTEK_DP_TEMP_CALIBRATION     27  /* value, signed, -6..+6 C */
#define KETOTEK_DP_CHILD_LOCK           40  /* bool */
#define KETOTEK_DP_SYSTEM_STATE         101 /* bool: on/off */
#define KETOTEK_DP_LOCAL_TEMP           102 /* value, x10 C */
#define KETOTEK_DP_HEATING_SETPOINT     103 /* value, x10 C */
#define KETOTEK_DP_VALVE_POSITION       104 /* value, 0-100% */
#define KETOTEK_DP_BATTERY_LOW          105 /* bool */
#define KETOTEK_DP_AWAY_MODE            106 /* bool */
#define KETOTEK_DP_SCHEDULE_MODE        107 /* enum */
#define KETOTEK_DP_SCHEDULE_ENABLE      108 /* bool */
#define KETOTEK_DP_WEEKLY_SCHEDULE_BASE 109 /* + 123..129, raw schedule bytes */

/* Minimum frame length for a DP report/set payload: 2 (seq) + 1 (dp_id) +
 * 1 (dp_type) + 2 (len) = 6 bytes before the DP's own data. */
#define TUYA_DP_HEADER_LEN 6

/* Decode a big-endian signed 32-bit value from a Tuya DP payload (used for
 * TUYA_DP_TYPE_VALUE DPs such as temperatures, x10 encoded). */
static inline int32_t tuya_dp_decode_value(const uint8_t *data)
{
    return (int32_t)(((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
                      ((uint32_t)data[2] << 8) | (uint32_t)data[3]);
}

/* Encode a signed 32-bit value as big-endian bytes into a Tuya DP payload. */
static inline void tuya_dp_encode_value(uint8_t *out, int32_t value)
{
    out[0] = (uint8_t)((value >> 24) & 0xFF);
    out[1] = (uint8_t)((value >> 16) & 0xFF);
    out[2] = (uint8_t)((value >> 8) & 0xFF);
    out[3] = (uint8_t)(value & 0xFF);
}
