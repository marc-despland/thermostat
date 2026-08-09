/*
 * Tuya cluster (0xEF00) protocol constants and KETOTEK KTF0177 DataPoint (DP)
 * table, shared between /gateway and /thermostat.
 *
 * Keep in sync with the identical copy in ../../thermostat/main/tuya_ketotek_dp.h.
 *
 * Wire format for cluster 0xEF00 custom commands:
 *   [SEQ_H][SEQ_L][DP_ID][DP_TYPE][LEN_H][LEN_L][DATA...]
 * Temperatures are x10 encoded (e.g. 21.5 C -> 215), big-endian.
 */
#pragma once

#include <stdint.h>

#define TUYA_CLUSTER_ID          0xEF00

/* Tuya cluster 0xEF00 command IDs. Per the official Tuya Zigbee Universal
 * Docking Access Standard (developer.tuya.com/en/docs/iot/
 * tuya-zigbee-universal-docking-access-standard?id=K9ik6zvofpzql), NOT the
 * old TUYA_CMD_QUERY=0x11 guess (see below) - see SPECIFICATIONS.md
 * "Protocole" for how that mistake was found. */
#define TUYA_CMD_SET_DATA        0x00 /* TY_DATA_REQUEST: gateway -> device data request */
#define TUYA_CMD_REPORT_1        0x01 /* TY_DATA_RESPONE: device -> gateway, response to a data request */
#define TUYA_CMD_REPORT_2        0x02 /* TY_DATA_REPORT: device -> gateway, spontaneous report (two-way) */
#define TUYA_CMD_QUERY           0x03 /* TY_DATA_QUERY: gateway -> device, query all current DPs - zero-length payload, no seq bytes even */
#define TUYA_CMD_MCU_VERSION_REQ 0x10 /* TUYA_MCU_VERSION_REQ: gateway -> device, query MCU firmware version */
#define TUYA_CMD_MCU_VERSION_RSP 0x11 /* TUYA_MCU_VERSION_RSP: device -> gateway, MCU firmware version (NOT a data query - this is what we mistakenly sent as "0x11 query" before) */
#define TUYA_CMD_TIME_SYNC       0x24 /* TUYA_MCU_SYNC_TIME: time sync request - TODO: unhandled */

typedef enum {
    TUYA_DP_TYPE_BOOL   = 0x01,
    TUYA_DP_TYPE_VALUE  = 0x02, /* int32, big-endian */
    TUYA_DP_TYPE_STRING = 0x03,
    TUYA_DP_TYPE_ENUM   = 0x04,
    TUYA_DP_TYPE_BITMAP = 0x05,
} tuya_dp_type_t;

/* KETOTEK KTF0177 DataPoints.
 *
 * This table is CONFIRMED: the real device's Basic cluster (0x0000) reports
 * manufacturerName="_TZE200_p3dbf6qs", modelIdentifier="TS0601" (read via
 * zcl_send_read_basic_attrs(), the "magic packet" probe - see
 * SPECIFICATIONS.md "Protocole"). That fingerprint is an exact match in
 * zigbee-herdsman-converters (old/documentation/zigbee-herdsman-converters/
 * src/devices/tuya.ts) for model "TS0601_thermostat_5", white-labelled as
 * "AVATTO ME167_1" - i.e. this KETOTEK-branded head is a rebadged AVATTO
 * ME167_1, NOT a Saswell/SEA801-802 device as previously assumed by
 * similarity. Table below is that converter's meta.tuyaDatapoints, taken
 * as ground truth (2026-08-09) - replaces the earlier Saswell-based guess
 * entirely. DP4/5/7 were already independently confirmed by hand on real
 * hardware before this was found; this source confirms them exactly and
 * adds the rest. */
#define KETOTEK_DP_SYSTEM_MODE            2   /* enum: auto=0, heat=1, off=2 */
#define KETOTEK_DP_RUNNING_STATE          3   /* enum: heat=0 (actively heating), idle=1 */
#define KETOTEK_DP_HEATING_SETPOINT       4   /* value, x10 C - read AND write (confirmed
                                                * on real hardware: writing this DP moves
                                                * the setpoint shown on the head's screen) */
#define KETOTEK_DP_LOCAL_TEMP             5   /* value, x10 C - confirmed on real hardware,
                                                * matches the temperature shown on screen */
#define KETOTEK_DP_CHILD_LOCK             7   /* bool - confirmed on real hardware by
                                                * toggling the child lock */
#define KETOTEK_DP_SCHEDULE_WEDNESDAY     28  /* raw schedule bytes, not decoded */
#define KETOTEK_DP_SCHEDULE_THURSDAY      29  /* raw schedule bytes, not decoded */
#define KETOTEK_DP_SCHEDULE_FRIDAY        30  /* raw schedule bytes, not decoded */
#define KETOTEK_DP_SCHEDULE_SATURDAY      31  /* raw schedule bytes, not decoded */
#define KETOTEK_DP_SCHEDULE_SUNDAY        32  /* raw schedule bytes, not decoded */
#define KETOTEK_DP_SCHEDULE_MONDAY        33  /* raw schedule bytes, not decoded */
#define KETOTEK_DP_SCHEDULE_TUESDAY       34  /* raw schedule bytes, not decoded */
#define KETOTEK_DP_ERROR_OR_BATTERY_LOW   35  /* composite/raw, not decoded */
#define KETOTEK_DP_FROST_PROTECTION       36  /* bool */
#define KETOTEK_DP_SCALE_PROTECTION       39  /* bool - anti-scaling auto-flush ("Ad" on screen) */
#define KETOTEK_DP_LOCAL_TEMP_CALIBRATION 47  /* value, signed */
#define KETOTEK_DP_PI_HEATING_DEMAND      101 /* raw - valve modulation, likely 0-100% */

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
