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
static OSStatus update_result;
static int ota_busy;
OSStatus mico_rtos_init_mutex(mico_mutex_t *m) { *m=1; return 0; }
OSStatus mico_rtos_lock_mutex(mico_mutex_t *m) { assert(*m); return 0; }
OSStatus mico_rtos_unlock_mutex(mico_mutex_t *m) { assert(*m); return 0; }
uint32_t mico_rtos_get_time(void) { return 0; }
mico_Context_t *mico_system_context_get(void) { return &context; }
void *mico_system_context_get_user_data(mico_Context_t *c) { assert(c==&context); return &flash; }
OSStatus mico_system_context_update(mico_Context_t *c) { assert(c==&context);writes++;return update_result; }
int recovery_ota_busy(void) { return ota_busy; }
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
    assert(sizeof(old)==604 && sizeof(old_v2)==608 && sizeof(current)==936);
    old.magic=OPENM1_CONFIG_MAGIC;old.version=1;old.size=sizeof(old);
    strcpy(old.host,"192.168.1.10");strcpy(old.username,"mqtt-user");
    strcpy(old.password,"private-test-value");strcpy(old.client_id,"OpenM1-12ABCD");
    strcpy(old.base_topic,"openm1/12ABCD");strcpy(old.discovery_prefix,"homeassistant");
    old.port=1883;old.publish_interval=5;old.mqtt_enabled=1;old.ha_enabled=1;
    old.crc32=old_crc(&old,sizeof(old));
    memset(&flash,0xff,sizeof(flash));memcpy(&flash,&old,sizeof(old));
    assert(config_store_init()==0 && writes==1);
    config_store_get(&current);
    assert(current.version==4 && current.brightness_level==4 && current.last_nonzero_brightness==4);
    assert(!current.proxy_enabled && current.proxy_port==1080);
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
    assert(current.version==4 && current.brightness_level==2 && current.last_nonzero_brightness==3);
    assert(!strcmp(current.host,old.host) && !strcmp(current.username,old.username));
    assert(!strcmp(current.password,old.password) && !strcmp(current.client_id,old.client_id));
    assert(!strcmp(current.base_topic,old.base_topic) && !strcmp(current.discovery_prefix,old.discovery_prefix));
    assert(current.mqtt_enabled==1 && current.ha_enabled==1);
    assert(!current.wifi_ssid[0] && !current.wifi_auto_connect && !current.ap_disable_after_sta_connected);
    /* The v0.6.18 v3 prefix survives a v4 boot without a Flash write. */
    strcpy(current.wifi_ssid,"SavedWiFi");
    strcpy(current.wifi_password,"saved-secret");
    current.wifi_auto_connect=1;
    current.version=3;current.size=708;
    current.crc32=old_crc(&current,708);
    memset(&flash,0xff,sizeof(flash));memcpy(&flash,&current,708);
    writes=0;
    assert(config_store_init()==0 && writes==0);
    config_store_get(&current);
    assert(current.version==4 && !strcmp(current.wifi_ssid,"SavedWiFi"));
    assert(!strcmp(current.wifi_password,"saved-secret") && current.wifi_auto_connect);
    assert(!current.proxy_enabled && current.proxy_port==1080);
    /* v0.6.19 v4 settings remain readable; the former AP password is ignored. */
    current.proxy_enabled=1;
    current.proxy_port=1080;
    strcpy(current.proxy_host,"192.0.2.20");
    memset(current.legacy_unused,'X',sizeof(current.legacy_unused));
    current.crc32=old_crc(&current,sizeof(current));
    flash=current;writes=0;
    assert(config_store_init()==0 && writes==0);
    config_store_get(&current);
    assert(current.proxy_enabled && !strcmp(current.proxy_host,"192.0.2.20"));
    assert(current.legacy_unused[0]==0 && current.legacy_unused[63]==0);
    assert(flash.legacy_unused[0]==0 && flash.legacy_unused[63]==0);
    strcpy(current.wifi_ssid,"TestWiFi");strcpy(current.wifi_password,"secret");
    current.wifi_auto_connect=1;current.ap_disable_after_sta_connected=1;
    assert(config_store_save(&current)==kNoErr);
    update_result=-5;
    assert(config_store_factory_reset()==-5);
    assert(!strcmp(flash.wifi_ssid,"TestWiFi") && !strcmp(flash.wifi_password,"secret"));
    config_store_get(&current);
    assert(!strcmp(current.wifi_ssid,"TestWiFi") && current.wifi_auto_connect);
    ota_busy=1;
    assert(config_store_factory_reset()==kNotPreparedErr);
    ota_busy=0;update_result=kNoErr;
    assert(config_store_factory_reset()==kNoErr);
    config_store_get(&current);
    assert(!current.wifi_ssid[0] && !current.wifi_password[0]);
    assert(!current.wifi_auto_connect && !current.ap_disable_after_sta_connected);
    assert(!current.host[0] && !current.username[0] && !current.password[0]);
    assert(!current.mqtt_enabled && !current.ha_enabled);
    assert(!strcmp(current.discovery_prefix,"homeassistant"));
    assert(!strcmp(current.client_id,"OpenM1-12ABCD"));
    assert(!strcmp(current.base_topic,"openm1/12ABCD"));
    assert(current.brightness_level==4 && current.last_nonzero_brightness==4);
    return 0;
}
