#include "homeassistant.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static char topics[6][160];
static char payloads[6][800];
static size_t lengths[6];
static unsigned publish_count;

const char *recovery_ssid(void) {return "OpenM1-ABCDEF";}
void wifi_manager_station_ip(char out[16]) {strcpy(out,"10.10.1.124");}
int ha_ota_config_complete(const char *url,const char *version,const char *sha)
{return url[0] && version[0] && strlen(sha)==64;}
int MQTTPublish(Client *client,const char *topic,MQTTMessage *message)
{
    unsigned index=publish_count++;
    (void)client;
    assert(index<6);
    assert(message->retained==1 && message->qos==QOS0);
    assert(message->payloadlen<sizeof(payloads[index]));
    strcpy(topics[index],topic);
    memcpy(payloads[index],message->payload,message->payloadlen);
    payloads[index][message->payloadlen]=0;
    lengths[index]=message->payloadlen;
    return MQTT_SUCCESS;
}
int main(void)
{
    openm1_config_t config={0};
    Client client={0};
    unsigned i;
    const char *sensors[]={"temperature","humidity","pm25","formaldehyde"};
    strcpy(config.base_topic,"openm1/ABCDEF");
    strcpy(config.discovery_prefix,"homeassistant");
    assert(homeassistant_publish(&client,&config,0)==0 && publish_count==6);
    for (i=0;i<4;i++) {
        char expected[160];
        snprintf(expected,sizeof(expected),"homeassistant/sensor/openm1_ABCDEF/%s/config",sensors[i]);
        assert(strcmp(topics[i],expected)==0);
        assert(lengths[i]>0 && strstr(payloads[i],"OpenM1 v0.6.22"));
    }
    assert(strcmp(topics[4],"homeassistant/number/openm1_ABCDEF/brightness/config")==0);
    for (i=0;i<8;i++) {
        const char *required[]={"屏幕亮度","openm1_ABCDEF_brightness","openm1/ABCDEF/brightness/set",
            "openm1/ABCDEF/state","value_json.brightness","\"min\":0","\"max\":4","\"step\":1"};
        assert(strstr(payloads[4],required[i]));
    }
    assert(strstr(payloads[4],"\"mode\":\"slider\""));
    assert(strstr(payloads[4],"openm1/ABCDEF/availability"));
    assert(strstr(payloads[4],"OpenM1 v0.6.22"));
    assert(strcmp(topics[5],"homeassistant/update/openm1_ABCDEF/firmware/config")==0);
    assert(lengths[5]==0); /* Disabled by default: remove any stale retained Update. */
    config.ha_ota_enabled=1;
    strcpy(config.ha_ota_url,"http://192.168.1.2/fw.ota.bin");
    strcpy(config.ha_ota_version,"0.6.23");
    memset(config.ha_ota_sha256,'a',64);config.ha_ota_sha256[64]=0;
    publish_count=0;
    assert(homeassistant_publish(&client,&config,0)==0 && publish_count==6);
    assert(strstr(payloads[5],"\"device_class\":\"firmware\""));
    assert(strstr(payloads[5],"openm1/ABCDEF/firmware/install"));
    assert(strstr(payloads[5],"openm1/ABCDEF/firmware/status"));
    assert(strstr(payloads[5],"\"payload_install\":\"INSTALL\""));
    publish_count=0;
    assert(homeassistant_publish(&client,&config,1)==0 && publish_count==6);
    for (i=0;i<6;i++) assert(lengths[i]==0 && payloads[i][0]==0);
    puts("PASS six retained HA discoveries and removal");
    return 0;
}
