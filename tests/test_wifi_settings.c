#include "wifi_settings.h"
#include <assert.h>
#include <string.h>

static int apply(openm1_config_t *c,const char *json)
{
    return wifi_settings_apply_json(json,strlen(json),c);
}

int main(void)
{
    openm1_config_t c;
    char output[256];
    memset(&c,0,sizeof(c));
    assert(apply(&c,"{\"ssid\":\"Home\",\"password\":\"secret\",\"auto_connect\":true}")==0);
    assert(!strcmp(c.wifi_ssid,"Home") && !strcmp(c.wifi_password,"secret"));
    assert(c.wifi_auto_connect && !c.ap_disable_after_sta_connected);
    c.ap_disable_after_sta_connected=1; /* saved by an older firmware */
    assert(!wifi_ap_policy_can_close(&c,1,1,0));
    assert(apply(&c,"{\"disable_ap_after_connect\":true}")==-5);
    assert(!wifi_ap_policy_can_close(&c,0,1,0)); /* no Station */
    assert(!wifi_ap_policy_can_close(&c,1,0,0)); /* different SSID */
    assert(!wifi_ap_policy_can_close(&c,1,1,1)); /* OTA busy */
    wifi_settings_json(&c,output,sizeof(output));
    assert(strstr(output,"Home") && !strstr(output,"secret"));
    assert(apply(&c,"{\"auto_connect\":false}")==0);
    assert(!c.wifi_auto_connect && !c.ap_disable_after_sta_connected);
    assert(!wifi_ap_policy_can_close(&c,1,1,0));
    assert(apply(&c,"{\"disable_ap_after_connect\":true}")==-5);
    assert(apply(&c,"{\"ssid\":\"Other\"}")==-1); /* password required on SSID change */
    assert(apply(&c,"{\"ssid\":\"Open\",\"password\":\"\",\"auto_connect\":true}")==0);
    assert(!strcmp(c.wifi_ssid,"Open") && !c.wifi_password[0]);
    assert(apply(&c,"{\"clear_saved_wifi\":true}")==0);
    assert(!c.wifi_ssid[0] && !c.wifi_password[0]);
    assert(!c.wifi_auto_connect && !c.ap_disable_after_sta_connected);
    return 0;
}
