#pragma once
#include "mico.h"
#include <stdint.h>

#define OPENM1_CONFIG_MAGIC 0x314d504fu
#define OPENM1_CONFIG_VERSION 3u
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t crc32;
    char host[129];
    char username[65];
    char password[129];
    char client_id[65];
    char base_topic[129];
    char discovery_prefix[65];
    uint16_t port;
    uint16_t publish_interval;
    uint8_t mqtt_enabled;
    uint8_t ha_enabled;
    uint8_t reserved[2];
    uint8_t brightness_level;
    uint8_t last_nonzero_brightness;
    uint8_t display_reserved[2];
    char wifi_ssid[32];
    char wifi_password[64];
    uint8_t wifi_auto_connect;
    uint8_t ap_disable_after_sta_connected;
    uint8_t wifi_reserved[2];
} openm1_config_t;

void config_store_defaults(openm1_config_t *config);
OSStatus config_store_init(void);
void config_store_get(openm1_config_t *out);
OSStatus config_store_save(const openm1_config_t *config);
OSStatus config_store_save_brightness(uint8_t brightness, uint8_t last_nonzero);
OSStatus config_store_factory_reset(void);
