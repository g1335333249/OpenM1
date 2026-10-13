#pragma once
#include "mico.h"
#include <stdint.h>

#define OPENM1_CONFIG_MAGIC 0x314d504fu
#define OPENM1_CONFIG_VERSION 5u
#define OPENM1_CONFIG_V4_SIZE 936u
#define OPENM1_NTP_DEFAULT_SERVER "ntp.aliyun.com"
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
    uint8_t proxy_enabled;
    uint8_t proxy_reserved;
    uint16_t proxy_port;
    char proxy_host[64];
    char proxy_username[32];
    char proxy_password[64];
    /* Layout compatibility only. Former v0.6.19 AP credential is never used. */
    uint8_t legacy_unused[64];
    uint8_t v4_trailing_padding[2];
    uint8_t ntp_enabled;
    uint8_t ha_ota_enabled;
    uint16_t ntp_interval_minutes;
    char ntp_server[65];
    char ha_ota_url[129];
    char ha_ota_version[32];
    char ha_ota_sha256[65];
} openm1_config_t;

void config_store_defaults(openm1_config_t *config);
OSStatus config_store_init(void);
void config_store_get(openm1_config_t *out);
void config_store_get_time_settings(char server[65],uint16_t *minutes,uint8_t *enabled);
OSStatus config_store_save(const openm1_config_t *config);
OSStatus config_store_save_brightness(uint8_t brightness, uint8_t last_nonzero);
OSStatus config_store_factory_reset(void);
