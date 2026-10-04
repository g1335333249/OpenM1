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
    uint32_t frame_count;
    uint32_t parser_error_count;
    uint32_t raw_bytes_seen;
} m1_sensor_snapshot_t;

OSStatus m1_sensor_init(void);
void m1_sensor_parse(const uint8_t *bytes, size_t length);
void m1_sensor_get_snapshot(m1_sensor_snapshot_t *out);
void m1_sensor_json(char *out, size_t capacity);
