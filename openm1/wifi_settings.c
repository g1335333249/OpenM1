#include "wifi_settings.h"
#include "json_min.h"
#include "wifi_station_logic.h"
#include <stdio.h>
#include <string.h>

static int valid_text(const char *value,size_t limit)
{
    size_t i,n=strlen(value);
    if (n>=limit) return 0;
    for (i=0;i<n;i++) if ((unsigned char)value[i]<32 || (unsigned char)value[i]==127) return 0;
    return 1;
}

int wifi_settings_apply_json(const char *body,size_t length,openm1_config_t *next)
{
    json_min_field_t fields[5];
    const json_min_field_t *ssid,*password,*auto_connect,*ap_off,*clear;
    int n,i;
    if (!body || !next) return -1;
    n=json_min_parse(body,length,fields,5);
    if (n<=0) return -1;
    for (i=0;i<n;i++)
        if (strcmp(fields[i].key,"ssid") && strcmp(fields[i].key,"password") &&
            strcmp(fields[i].key,"auto_connect") &&
            strcmp(fields[i].key,"disable_ap_after_connect") &&
            strcmp(fields[i].key,"clear_saved_wifi")) return -1;
    ssid=json_min_find(fields,n,"ssid");
    password=json_min_find(fields,n,"password");
    auto_connect=json_min_find(fields,n,"auto_connect");
    ap_off=json_min_find(fields,n,"disable_ap_after_connect");
    clear=json_min_find(fields,n,"clear_saved_wifi");
    if (clear) {
        if (n!=1 || clear->kind!='b' || strcmp(clear->value,"true")) return -1;
        memset(next->wifi_ssid,0,sizeof(next->wifi_ssid));
        memset(next->wifi_password,0,sizeof(next->wifi_password));
        next->wifi_auto_connect=0;
        next->ap_disable_after_sta_connected=0;
        return 0;
    }
    if ((ssid && (ssid->kind!='s' || !ssid->value[0] ||
                  !valid_text(ssid->value,sizeof(next->wifi_ssid)))) ||
        (password && (password->kind!='s' ||
                      !valid_text(password->value,sizeof(next->wifi_password)))) ||
        (auto_connect && auto_connect->kind!='b') ||
        (ap_off && ap_off->kind!='b')) return -1;
    if (ssid && strcmp(ssid->value,next->wifi_ssid) && !password) return -1;
    if (ssid) { memset(next->wifi_ssid,0,sizeof(next->wifi_ssid));
                strcpy(next->wifi_ssid,ssid->value); }
    if (password) { memset(next->wifi_password,0,sizeof(next->wifi_password));
                    strcpy(next->wifi_password,password->value); }
    if (password && !next->wifi_ssid[0]) return -1;
    if (auto_connect) next->wifi_auto_connect=!strcmp(auto_connect->value,"true");
    if (!next->wifi_auto_connect) next->ap_disable_after_sta_connected=0;
    if (ap_off) {
        if (!strcmp(ap_off->value,"true")) return -5;
        next->ap_disable_after_sta_connected=0;
    }
    if (next->wifi_auto_connect && !next->wifi_ssid[0]) return -1;
    return 0;
}

int wifi_ap_policy_can_close(const openm1_config_t *config,int station_ready,
                             int saved_ssid_matches,int ota_busy)
{
    return WIFI_AP_AUTO_CLOSE_SUPPORTED && config && config->wifi_auto_connect && config->ap_disable_after_sta_connected &&
           config->wifi_ssid[0] && station_ready && saved_ssid_matches && !ota_busy;
}

static void json_escape(const char *in,char *out,size_t capacity)
{
    size_t used=0;
    while (*in && used+3<capacity) {
        unsigned char c=(unsigned char)*in++;
        if (c=='"' || c=='\\') out[used++]='\\';
        out[used++]=(char)c;
    }
    if (capacity) out[used]=0;
}
void wifi_settings_json(const openm1_config_t *config,char *out,size_t capacity)
{
    char ssid[66];
    if (!out || !capacity || !config) return;
    json_escape(config->wifi_ssid,ssid,sizeof(ssid));
    snprintf(out,capacity,
      "{\"saved_ssid\":\"%s\",\"credential_saved\":%s,\"password_nonempty\":%s,"
      "\"auto_connect\":%s,\"disable_ap_after_connect\":false,\"ap_auto_close_supported\":false,\"legacy_saved_ap_close_requested\":%s}",
      ssid,config->wifi_ssid[0]?"true":"false",config->wifi_password[0]?"true":"false",
      config->wifi_auto_connect?"true":"false",
      config->ap_disable_after_sta_connected?"true":"false");
}
