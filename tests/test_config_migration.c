#include "config_store.h"
#include <assert.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    uint32_t magic;
    uint16_t version,size;
    uint32_t crc32;
    char host[129],username[65],password[129],client_id[65],base_topic[129],discovery_prefix[65];
    uint16_t port,publish_interval;
    uint8_t mqtt_enabled,ha_enabled,reserved[2];
} old_config_t;
typedef struct {
    uint32_t magic;
    uint16_t version,size;
    uint32_t crc32;
    char host[129],username[65],password[129],client_id[65],base_topic[129],discovery_prefix[65];
    uint16_t port,publish_interval;
    uint8_t mqtt_enabled,ha_enabled,reserved[2];
    uint8_t brightness_level,last_nonzero_brightness,display_reserved[2];
} old_config_v2_t;

static mico_Context_t context;
static openm1_config_t flash;
static unsigned writes;
OSStatus mico_rtos_init_mutex(mico_mutex_t *m) { *m=1; return 0; }
OSStatus mico_rtos_lock_mutex(mico_mutex_t *m) { assert(*m); return 0; }
OSStatus mico_rtos_unlock_mutex(mico_mutex_t *m) { assert(*m); return 0; }
uint32_t mico_rtos_get_time(void) { return 0; }
mico_Context_t *mico_system_context_get(void) { return &context; }
void *mico_system_context_get_user_data(mico_Context_t *c) { assert(c==&context); return &flash; }
OSStatus mico_system_context_update(mico_Context_t *c) { assert(c==&context);writes++;return 0; }
int recovery_ota_busy(void) { return 0; }
const char *recovery_ssid(void) { return "OpenM1-12ABCD"; }

static uint32_t old_crc(const void *old,size_t length)
{
    const uint8_t *p=(const uint8_t *)old;
    uint32_t crc=0xffffffffu;
    size_t i,j;
    for (i=offsetof(old_config_t,host);i<length;i++) {
        crc^=p[i];
        for (j=0;j<8;j++) crc=(crc>>1)^((crc&1u)?0xedb88320u:0u);
    }
    return ~crc;
}

int main(void)
{
    old_config_t old;
    old_config_v2_t old_v2;
    openm1_config_t current;
    memset(&old,0,sizeof(old));
    assert(sizeof(old)==604 && sizeof(old_v2)==608 && sizeof(current)==708);
    old.magic=OPENM1_CONFIG_MAGIC;old.version=1;old.size=sizeof(old);
    strcpy(old.host,"192.168.1.10");strcpy(old.username,"mqtt-user");
    strcpy(old.password,"private-test-value");strcpy(old.client_id,"OpenM1-12ABCD");
    strcpy(old.base_topic,"openm1/12ABCD");strcpy(old.discovery_prefix,"homeassistant");
    old.port=1883;old.publish_interval=5;old.mqtt_enabled=1;old.ha_enabled=1;
    old.crc32=old_crc(&old,sizeof(old));
    memset(&flash,0xff,sizeof(flash));memcpy(&flash,&old,sizeof(old));
    assert(config_store_init()==0 && writes==1);
    config_store_get(&current);
    assert(current.version==3 && current.brightness_level==4 && current.last_nonzero_brightness==4);
    assert(!strcmp(current.host,old.host) && !strcmp(current.password,old.password));
    assert(current.mqtt_enabled==1 && current.ha_enabled==1);
    assert(!current.wifi_ssid[0] && !current.wifi_password[0]);
    assert(!current.wifi_auto_connect && !current.ap_disable_after_sta_connected);
    assert(config_store_save_brightness(0,3)==0 && writes==2);
    config_store_get(&current);
    assert(current.brightness_level==0 && current.last_nonzero_brightness==3);
    assert(!strcmp(current.password,old.password) && current.ha_enabled==1);
    memset(&old_v2,0,sizeof(old_v2));
    memcpy(&old_v2,&old,sizeof(old));
    old_v2.version=2;old_v2.size=sizeof(old_v2);
    old_v2.brightness_level=2;old_v2.last_nonzero_brightness=3;
    old_v2.crc32=old_crc(&old_v2,sizeof(old_v2));
    memset(&flash,0xff,sizeof(flash));memcpy(&flash,&old_v2,sizeof(old_v2));
    writes=0;
    assert(config_store_init()==0 && writes==1);
    config_store_get(&current);
    assert(current.version==3 && current.brightness_level==2 && current.last_nonzero_brightness==3);
    assert(!strcmp(current.host,old.host) && !strcmp(current.username,old.username));
    assert(!strcmp(current.password,old.password) && !strcmp(current.client_id,old.client_id));
    assert(!strcmp(current.base_topic,old.base_topic) && !strcmp(current.discovery_prefix,old.discovery_prefix));
    assert(current.mqtt_enabled==1 && current.ha_enabled==1);
    assert(!current.wifi_ssid[0] && !current.wifi_auto_connect && !current.ap_disable_after_sta_connected);
    return 0;
}
