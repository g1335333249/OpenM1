#include "config_store.h"
#include "recovery.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static mico_mutex_t config_mutex;
static int config_ready;
static openm1_config_t config_cache;

static uint32_t config_crc32(const openm1_config_t *config)
{
    const uint8_t *bytes=(const uint8_t *)config;
    uint32_t crc=0xffffffffu;
    size_t i,j;
    for (i=offsetof(openm1_config_t,host);i<sizeof(*config);i++) {
        crc^=bytes[i];
        for (j=0;j<8;j++) crc=(crc>>1)^((crc&1u)?0xedb88320u:0u);
    }
    return ~crc;
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
           config->publish_interval>=2 && config->publish_interval<=300;
}
OSStatus config_store_init(void)
{
    openm1_config_t *saved=(openm1_config_t *)mico_system_context_get_user_data(mico_system_context_get());
    OSStatus err;
    if (!saved) return kNotPreparedErr;
    err=mico_rtos_init_mutex(&config_mutex);
    if (err!=kNoErr) return err;
    if (config_valid(saved) && !(saved->host[0]==0 &&
        !strcmp(saved->client_id,RECOVERY_FALLBACK_SSID))) config_cache=*saved;
    else {
        config_store_defaults(&config_cache);
        printf("CONFIG: using defaults; no parameter flash write\r\n");
    }
    config_ready=1;
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
