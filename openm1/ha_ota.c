#include "ha_ota.h"
#include "config_store.h"
#include "json_min.h"
#include "recovery.h"
#include <stdio.h>
#include <string.h>

int ha_ota_configure(const char *body,size_t length)
{
    openm1_config_t config;
    json_min_field_t fields[4];
    int count,i,seen_enabled=0,seen_url=0,seen_version=0,seen_sha=0;
    if (!body || length>600 || recovery_ota_busy()) return -2;
    count=json_min_parse(body,length,fields,4);
    if (count<0) return -1;
    config_store_get(&config);
    if (count==1 && !strcmp(fields[0].key,"clear") && fields[0].kind=='b' &&
        !strcmp(fields[0].value,"true")) {
        config.ha_ota_enabled=0;config.ha_ota_url[0]=0;
        config.ha_ota_version[0]=0;config.ha_ota_sha256[0]=0;
        return config_store_save(&config)==kNoErr?0:-2;
    }
    if (count!=4) return -1;
    for (i=0;i<count;i++) {
        json_min_field_t *f=&fields[i];
        if (!strcmp(f->key,"enabled") && f->kind=='b') {
            config.ha_ota_enabled=!strcmp(f->value,"true");seen_enabled=1;
        } else if (!strcmp(f->key,"url") && f->kind=='s' && strlen(f->value)<sizeof(config.ha_ota_url)) {
            snprintf(config.ha_ota_url,sizeof(config.ha_ota_url),"%s",f->value);seen_url=1;
        } else if (!strcmp(f->key,"target_version") && f->kind=='s' &&
            strlen(f->value)<sizeof(config.ha_ota_version)) {
            snprintf(config.ha_ota_version,sizeof(config.ha_ota_version),"%s",f->value);seen_version=1;
        } else if (!strcmp(f->key,"sha256") && f->kind=='s' &&
            strlen(f->value)<sizeof(config.ha_ota_sha256)) {
            snprintf(config.ha_ota_sha256,sizeof(config.ha_ota_sha256),"%s",f->value);seen_sha=1;
        } else return -1;
    }
    if (!seen_enabled || !seen_url || !seen_version || !seen_sha ||
        !ha_ota_config_complete(config.ha_ota_url,config.ha_ota_version,config.ha_ota_sha256)) return -1;
    return config_store_save(&config)==kNoErr?0:-2;
}
void ha_ota_config_json(char *out,size_t capacity)
{
    openm1_config_t config;
    config_store_get(&config);
    snprintf(out,capacity,"{\"enabled\":%s,\"configured\":%s,\"url\":\"%s\","
        "\"target_version\":\"%s\",\"sha256\":\"%s\",\"http_lan_only\":true}",
        config.ha_ota_enabled?"true":"false",
        ha_ota_config_complete(config.ha_ota_url,config.ha_ota_version,config.ha_ota_sha256)?"true":"false",
        config.ha_ota_url,config.ha_ota_version,config.ha_ota_sha256);
}
