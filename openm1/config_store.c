#include "config_store.h"
#include "recovery.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static mico_mutex_t config_mutex;
static int config_ready;
static openm1_config_t config_cache;
typedef char config_v4_prefix_must_match[(offsetof(openm1_config_t,ntp_enabled)==OPENM1_CONFIG_V4_SIZE)?1:-1];

/* v0.4.1 layout, including its two trailing alignment bytes. */
typedef struct {
    uint32_t magic;
    uint16_t version,size;
    uint32_t crc32;
    char host[129],username[65],password[129],client_id[65],base_topic[129],discovery_prefix[65];
    uint16_t port,publish_interval;
    uint8_t mqtt_enabled,ha_enabled,reserved[2];
} openm1_config_v1_t;

/* Exact v0.5.2 layout. Keep this prefix unchanged when extending v3. */
typedef struct {
    uint32_t magic;
    uint16_t version,size;
    uint32_t crc32;
    char host[129],username[65],password[129],client_id[65],base_topic[129],discovery_prefix[65];
    uint16_t port,publish_interval;
    uint8_t mqtt_enabled,ha_enabled,reserved[2];
    uint8_t brightness_level,last_nonzero_brightness,display_reserved[2];
} openm1_config_v2_t;
/* v0.6.18 on-flash prefix. Proxy settings are appended after this layout. */
typedef struct {
    uint32_t magic;
    uint16_t version,size;
    uint32_t crc32;
    char host[129],username[65],password[129],client_id[65],base_topic[129],discovery_prefix[65];
    uint16_t port,publish_interval;
    uint8_t mqtt_enabled,ha_enabled,reserved[2];
    uint8_t brightness_level,last_nonzero_brightness,display_reserved[2];
    char wifi_ssid[32],wifi_password[64];
    uint8_t wifi_auto_connect,ap_disable_after_sta_connected,wifi_reserved[2];
} openm1_config_v3_t;

static uint32_t crc32_bytes(const void *data, size_t length)
{
    const uint8_t *bytes=(const uint8_t *)data;
    uint32_t crc=0xffffffffu;
    size_t i,j;
    for (i=offsetof(openm1_config_v1_t,host);i<length;i++) {
        crc^=bytes[i];
        for (j=0;j<8;j++) crc=(crc>>1)^((crc&1u)?0xedb88320u:0u);
    }
    return ~crc;
}
static uint32_t config_crc32(const openm1_config_t *config)
{
    return crc32_bytes(config,sizeof(*config));
}
void config_store_defaults(openm1_config_t *config)
{
    const char *ssid=recovery_ssid();
    const char *suffix=strrchr(ssid,'-');
    memset(config,0,sizeof(*config));
    config->magic=OPENM1_CONFIG_MAGIC;
    config->version=OPENM1_CONFIG_VERSION;
    config->size=sizeof(*config);
    config->port=1883;
    config->publish_interval=5;
    config->brightness_level=4;
    config->last_nonzero_brightness=4;
    config->proxy_port=1080;
    config->ntp_enabled=1;
    config->ntp_interval_minutes=360;
    snprintf(config->ntp_server,sizeof(config->ntp_server),"%s",OPENM1_NTP_DEFAULT_SERVER);
    snprintf(config->discovery_prefix,sizeof(config->discovery_prefix),"homeassistant");
    snprintf(config->client_id,sizeof(config->client_id),"%s",ssid);
    snprintf(config->base_topic,sizeof(config->base_topic),"openm1/%s",suffix?suffix+1:"RECOVERY");
    config->crc32=config_crc32(config);
}
void appRestoreDefault_callback(void *user_data, uint32_t size)
{
    if (user_data && size==sizeof(openm1_config_t))
        config_store_defaults((openm1_config_t *)user_data);
}
static int config_valid(const openm1_config_t *config)
{
    if (config->magic!=OPENM1_CONFIG_MAGIC || config->version!=OPENM1_CONFIG_VERSION ||
        config->size!=sizeof(*config) || config->crc32!=config_crc32(config)) return 0;
    return config->host[128]==0 && config->username[64]==0 && config->password[128]==0 &&
           config->client_id[64]==0 && config->base_topic[128]==0 &&
           config->discovery_prefix[64]==0 && config->port>0 &&
           config->publish_interval>=2 && config->publish_interval<=300 &&
           config->brightness_level<=4 && config->last_nonzero_brightness>=1 &&
           config->last_nonzero_brightness<=4 &&
           config->wifi_ssid[31]==0 && config->wifi_password[63]==0 &&
           config->wifi_auto_connect<=1 && config->ap_disable_after_sta_connected<=1 &&
           config->proxy_enabled<=1 && config->proxy_port>0 &&
           config->proxy_host[63]==0 && config->proxy_username[31]==0 &&
           config->proxy_password[63]==0 &&
           config->ntp_enabled<=1 && config->ha_ota_enabled<=1 &&
           config->ntp_interval_minutes>=15 && config->ntp_interval_minutes<=1440 &&
           config->ntp_server[64]==0 && config->ntp_server[0]!=0 &&
           config->ha_ota_url[128]==0 && config->ha_ota_version[31]==0 &&
           config->ha_ota_sha256[64]==0 &&
           (!config->ap_disable_after_sta_connected ||
            (config->wifi_auto_connect && config->wifi_ssid[0]));
}
static int config_v4_valid(const openm1_config_t *old)
{
    return old->magic==OPENM1_CONFIG_MAGIC && old->version==4 &&
        old->size==OPENM1_CONFIG_V4_SIZE &&
        old->crc32==crc32_bytes(old,OPENM1_CONFIG_V4_SIZE) &&
        old->host[128]==0 && old->username[64]==0 && old->password[128]==0 &&
        old->client_id[64]==0 && old->base_topic[128]==0 &&
        old->discovery_prefix[64]==0 && old->wifi_ssid[31]==0 &&
        old->wifi_password[63]==0 && old->proxy_host[63]==0 &&
        old->proxy_username[31]==0 && old->proxy_password[63]==0 &&
        old->port>0 && old->publish_interval>=2 && old->publish_interval<=300 &&
        old->brightness_level<=4 && old->last_nonzero_brightness>=1 &&
        old->last_nonzero_brightness<=4 && old->wifi_auto_connect<=1 &&
        old->proxy_enabled<=1 && old->proxy_port>0;
}
static int config_v3_valid(const openm1_config_v3_t *old)
{
    return old->magic==OPENM1_CONFIG_MAGIC && old->version==3 &&
        old->size==sizeof(*old) && old->crc32==crc32_bytes(old,sizeof(*old)) &&
        old->host[128]==0 && old->username[64]==0 && old->password[128]==0 &&
        old->client_id[64]==0 && old->base_topic[128]==0 &&
        old->discovery_prefix[64]==0 && old->wifi_ssid[31]==0 &&
        old->wifi_password[63]==0 && old->port>0 &&
        old->publish_interval>=2 && old->publish_interval<=300 &&
        old->brightness_level<=4 && old->last_nonzero_brightness>=1 &&
        old->last_nonzero_brightness<=4 && old->wifi_auto_connect<=1 &&
        old->ap_disable_after_sta_connected<=1;
}
static int config_v2_valid(const openm1_config_v2_t *old)
{
    return old->magic==OPENM1_CONFIG_MAGIC && old->version==2 &&
           old->size==sizeof(*old) && old->crc32==crc32_bytes(old,sizeof(*old)) &&
           old->host[128]==0 && old->username[64]==0 && old->password[128]==0 &&
           old->client_id[64]==0 && old->base_topic[128]==0 &&
           old->discovery_prefix[64]==0 && old->port>0 &&
           old->publish_interval>=2 && old->publish_interval<=300 &&
           old->brightness_level<=4 && old->last_nonzero_brightness>=1 &&
           old->last_nonzero_brightness<=4;
}
static int config_v1_valid(const openm1_config_v1_t *old)
{
    return old->magic==OPENM1_CONFIG_MAGIC && old->version==1 &&
           old->size==sizeof(*old) && old->crc32==crc32_bytes(old,sizeof(*old)) &&
           old->host[128]==0 && old->username[64]==0 && old->password[128]==0 &&
           old->client_id[64]==0 && old->base_topic[128]==0 &&
           old->discovery_prefix[64]==0 && old->port>0 &&
           old->publish_interval>=2 && old->publish_interval<=300;
}
OSStatus config_store_init(void)
{
    openm1_config_t *saved=(openm1_config_t *)mico_system_context_get_user_data(mico_system_context_get());
    OSStatus err;
    int migrate=0,defer_migration=0;
    if (!saved) return kNotPreparedErr;
    err=mico_rtos_init_mutex(&config_mutex);
    if (err!=kNoErr) return err;
    if (config_valid(saved) && !(saved->host[0]==0 &&
        !strcmp(saved->client_id,RECOVERY_FALLBACK_SSID))) {
        /* v0.6.19 used this tail for an AP password. Never use or retain it
         * in the active v0.6.20 configuration. No boot-time Flash write. */
        memset(saved->legacy_unused,0,sizeof(saved->legacy_unused));
        saved->crc32=config_crc32(saved);
        config_cache=*saved;
    }
    else if (config_v4_valid(saved)) {
        config_store_defaults(&config_cache);
        memcpy(&config_cache,saved,OPENM1_CONFIG_V4_SIZE);
        memset(config_cache.legacy_unused,0,sizeof(config_cache.legacy_unused));
        migrate=1;defer_migration=1;
        printf("CONFIG: migrating v4 settings to v5 in RAM; persistence deferred\r\n");
    }
    else if (config_v3_valid((const openm1_config_v3_t *)saved)) {
        config_store_defaults(&config_cache);
        memcpy(&config_cache,saved,sizeof(openm1_config_v3_t));
        migrate=1;defer_migration=1;
        printf("CONFIG: migrating v3 settings to v5 layout\r\n");
    }
    else if (config_v2_valid((const openm1_config_v2_t *)saved)) {
        config_store_defaults(&config_cache);
        memcpy(&config_cache,saved,sizeof(openm1_config_v2_t));
        migrate=1;
        printf("CONFIG: migrating v2 MQTT/HA/brightness settings to v5\r\n");
    }
    else if (config_v1_valid((const openm1_config_v1_t *)saved)) {
        config_store_defaults(&config_cache);
        /* Preserve every v0.4.1 MQTT and HA byte; only append display fields. */
        memcpy(&config_cache,saved,sizeof(openm1_config_v1_t));
        config_cache.brightness_level=4;
        config_cache.last_nonzero_brightness=4;
        migrate=1;
        printf("CONFIG: migrating v1 MQTT/HA settings to v5\r\n");
    }
    else {
        config_store_defaults(&config_cache);
        printf("CONFIG: using defaults; no parameter flash write\r\n");
    }
    config_ready=1;
    if (migrate) {
        /* A cold boot should not rewrite parameter Flash just to change the
         * struct layout. The next explicit settings save persists v4. */
        config_cache.magic=OPENM1_CONFIG_MAGIC;
        config_cache.version=OPENM1_CONFIG_VERSION;
        config_cache.size=sizeof(config_cache);
        config_cache.crc32=config_crc32(&config_cache);
        if (defer_migration) printf("CONFIG: migration in RAM; persistence deferred\r\n");
        else {
            err=config_store_save(&config_cache);
            if (err!=kNoErr) printf("CONFIG: migration save failed = %d; settings retained in RAM\r\n",err);
        }
    }
    return kNoErr;
}
void config_store_get(openm1_config_t *out)
{
    if (!config_ready) { config_store_defaults(out); return; }
    mico_rtos_lock_mutex(&config_mutex);
    *out=config_cache;
    mico_rtos_unlock_mutex(&config_mutex);
}
void config_store_get_time_settings(char server[65],uint16_t *minutes,uint8_t *enabled)
{
    if (!config_ready) {
        snprintf(server,65,"%s",OPENM1_NTP_DEFAULT_SERVER);
        *minutes=360;*enabled=1;return;
    }
    mico_rtos_lock_mutex(&config_mutex);
    memcpy(server,config_cache.ntp_server,65);
    *minutes=config_cache.ntp_interval_minutes;
    *enabled=config_cache.ntp_enabled;
    mico_rtos_unlock_mutex(&config_mutex);
}
OSStatus config_store_save(const openm1_config_t *config)
{
    openm1_config_t *saved;
    OSStatus err;
    if (!config_ready || !config || recovery_ota_busy()) return kNotPreparedErr;
    mico_rtos_lock_mutex(&config_mutex);
    saved=(openm1_config_t *)mico_system_context_get_user_data(mico_system_context_get());
    if (!saved) {mico_rtos_unlock_mutex(&config_mutex);return kNotPreparedErr;}
    /* The context buffer is the serialized write candidate. The valid cache
     * remains available for rollback if validation or Flash update fails. */
    *saved=*config;
    saved->magic=OPENM1_CONFIG_MAGIC;
    saved->version=OPENM1_CONFIG_VERSION;
    saved->size=sizeof(*saved);
    saved->crc32=config_crc32(saved);
    if (!config_valid(saved)) {*saved=config_cache;mico_rtos_unlock_mutex(&config_mutex);return kParamErr;}
    err=mico_system_context_update(mico_system_context_get());
    if (err==kNoErr) config_cache=*saved;
    else *saved=config_cache;
    mico_rtos_unlock_mutex(&config_mutex);
    return err;
}
OSStatus config_store_save_brightness(uint8_t brightness, uint8_t last_nonzero)
{
    openm1_config_t *saved;
    OSStatus err;
    if (!config_ready || recovery_ota_busy() || brightness>4 ||
        last_nonzero<1 || last_nonzero>4) return kParamErr;
    mico_rtos_lock_mutex(&config_mutex);
    saved=(openm1_config_t *)mico_system_context_get_user_data(mico_system_context_get());
    if (!saved) {mico_rtos_unlock_mutex(&config_mutex);return kNotPreparedErr;}
    *saved=config_cache;
    saved->brightness_level=brightness;
    saved->last_nonzero_brightness=last_nonzero;
    saved->crc32=config_crc32(saved);
    err=mico_system_context_update(mico_system_context_get());
    if (err==kNoErr) config_cache=*saved;
    else *saved=config_cache;
    mico_rtos_unlock_mutex(&config_mutex);
    return err;
}

OSStatus config_store_factory_reset(void)
{
    openm1_config_t *saved;
    OSStatus err;
    /* Keep a 708-byte configuration off the 3072-byte housekeeping stack.
     * config_cache is the last effective valid configuration and is retained
     * until the context update has actually succeeded. */
    if (!config_ready || recovery_ota_busy()) return kNotPreparedErr;
    mico_rtos_lock_mutex(&config_mutex);
    saved=(openm1_config_t *)mico_system_context_get_user_data(mico_system_context_get());
    if (!saved) { mico_rtos_unlock_mutex(&config_mutex); return kNotPreparedErr; }
    config_store_defaults(saved);
    err=mico_system_context_update(mico_system_context_get());
    if (err==kNoErr) config_cache=*saved;
    else *saved=config_cache;
    mico_rtos_unlock_mutex(&config_mutex);
    return err;
}
