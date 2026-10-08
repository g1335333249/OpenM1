#include "config_store.h"
#include "recovery.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static mico_mutex_t config_mutex;
static int config_ready;
static openm1_config_t config_cache;

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
           (!config->ap_disable_after_sta_connected ||
            (config->wifi_auto_connect && config->wifi_ssid[0]));
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
    int migrate=0;
    if (!saved) return kNotPreparedErr;
    err=mico_rtos_init_mutex(&config_mutex);
    if (err!=kNoErr) return err;
    if (config_valid(saved) && !(saved->host[0]==0 &&
        !strcmp(saved->client_id,RECOVERY_FALLBACK_SSID))) config_cache=*saved;
    else if (config_v2_valid((const openm1_config_v2_t *)saved)) {
        config_store_defaults(&config_cache);
        memcpy(&config_cache,saved,sizeof(openm1_config_v2_t));
        migrate=1;
        printf("CONFIG: migrating v2 MQTT/HA/brightness settings to v3\r\n");
    }
    else if (config_v1_valid((const openm1_config_v1_t *)saved)) {
        config_store_defaults(&config_cache);
        /* Preserve every v0.4.1 MQTT and HA byte; only append display fields. */
        memcpy(&config_cache,saved,sizeof(openm1_config_v1_t));
        config_cache.brightness_level=4;
        config_cache.last_nonzero_brightness=4;
        migrate=1;
        printf("CONFIG: migrating v1 MQTT/HA settings to v3\r\n");
    }
    else {
        config_store_defaults(&config_cache);
        printf("CONFIG: using defaults; no parameter flash write\r\n");
    }
    config_ready=1;
    if (migrate) {
        err=config_store_save(&config_cache);
        if (err!=kNoErr) printf("CONFIG: migration save failed = %d; settings retained in RAM\r\n",err);
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
OSStatus config_store_save(const openm1_config_t *config)
{
    openm1_config_t next,previous;
    openm1_config_t *saved;
    OSStatus err;
    if (!config_ready || !config || recovery_ota_busy()) return kNotPreparedErr;
    next=*config;
    next.magic=OPENM1_CONFIG_MAGIC;
    next.version=OPENM1_CONFIG_VERSION;
    next.size=sizeof(next);
    next.crc32=config_crc32(&next);
    if (!config_valid(&next)) return kParamErr;
    mico_rtos_lock_mutex(&config_mutex);
    saved=(openm1_config_t *)mico_system_context_get_user_data(mico_system_context_get());
    previous=*saved;
    *saved=next;
    err=mico_system_context_update(mico_system_context_get());
    if (err==kNoErr) config_cache=next;
    else *saved=previous;
    mico_rtos_unlock_mutex(&config_mutex);
    return err;
}
OSStatus config_store_save_brightness(uint8_t brightness, uint8_t last_nonzero)
{
    openm1_config_t next,previous;
    openm1_config_t *saved;
    OSStatus err;
    if (!config_ready || recovery_ota_busy() || brightness>4 ||
        last_nonzero<1 || last_nonzero>4) return kParamErr;
    mico_rtos_lock_mutex(&config_mutex);
    saved=(openm1_config_t *)mico_system_context_get_user_data(mico_system_context_get());
    previous=*saved;
    next=config_cache;
    next.brightness_level=brightness;
    next.last_nonzero_brightness=last_nonzero;
    next.magic=OPENM1_CONFIG_MAGIC;
    next.version=OPENM1_CONFIG_VERSION;
    next.size=sizeof(next);
    next.crc32=config_crc32(&next);
    *saved=next;
    err=mico_system_context_update(mico_system_context_get());
    if (err==kNoErr) config_cache=next;
    else *saved=previous;
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
