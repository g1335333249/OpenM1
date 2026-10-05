#pragma once
#include "mico.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    float temperature;
    float humidity;
    uint16_t pm25;
    float formaldehyde;
    bool temperature_valid;
    bool humidity_valid;
    bool pm25_valid;
    bool formaldehyde_valid;
    uint32_t last_update_ms;
    uint32_t frame_count; /* Legacy /api/sensors field: type 0x01 only. */
    uint32_t total_frames;
    uint32_t sensor_frames;
    uint32_t time_frames;
    uint32_t type0f_frames;
    uint32_t type18_frames;
    uint32_t unknown_frames;
    uint32_t invalid_frames;
    char m1_datetime[20];
    uint8_t type0f_last_value;
    uint32_t type0f_last_rx_ms;
    uint32_t parser_error_count;
    uint32_t raw_bytes_seen;
} m1_sensor_snapshot_t;

OSStatus m1_sensor_init(void);
void m1_sensor_parse(const uint8_t *bytes, size_t length);
void m1_sensor_set_brightness_callback(void (*callback)(uint8_t));
void m1_sensor_get_snapshot(m1_sensor_snapshot_t *out);
void m1_sensor_json(char *out, size_t capacity);
