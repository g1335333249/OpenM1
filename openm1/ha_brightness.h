#pragma once
#include <stddef.h>
#include <stdint.h>

/* MQTT payloads are byte strings, not C strings. Only one ASCII digit is valid. */
int ha_brightness_parse(const void *payload,size_t length,uint8_t *level);
typedef struct {
    uint8_t level;
    uint8_t pending;
    uint8_t invalid;
} ha_brightness_pending_t;
void ha_brightness_enqueue(volatile ha_brightness_pending_t *pending,const void *payload,size_t length);
int ha_brightness_take(volatile ha_brightness_pending_t *pending,int ota_busy,uint8_t *level);
int ha_brightness_take_invalid(volatile ha_brightness_pending_t *pending);
int ha_brightness_format_state(char *out,size_t capacity,
    const char *temperature,const char *humidity,const char *pm25,const char *formaldehyde,
    uint8_t brightness,uint32_t uptime_seconds,int rssi);
