#include "homeassistant.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static char topics[5][160];
static char payloads[5][800];
static size_t lengths[5];
static unsigned publish_count;

const char *recovery_ssid(void) {return "OpenM1-ABCDEF";}
void wifi_manager_station_ip(char out[16]) {strcpy(out,"10.10.1.124");}
int MQTTPublish(Client *client,const char *topic,MQTTMessage *message)
{
    unsigned index=publish_count++;
    (void)client;
    assert(index<5);
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
    assert(homeassistant_publish(&client,&config,0)==0 && publish_count==5);
    for (i=0;i<4;i++) {
        char expected[160];
        snprintf(expected,sizeof(expected),"homeassistant/sensor/openm1_ABCDEF/%s/config",sensors[i]);
        assert(strcmp(topics[i],expected)==0);
        assert(lengths[i]>0 && strstr(payloads[i],"OpenM1 v0.6.13"));
    }
    assert(strcmp(topics[4],"homeassistant/number/openm1_ABCDEF/brightness/config")==0);
    for (i=0;i<8;i++) {
        const char *required[]={"屏幕亮度","openm1_ABCDEF_brightness","openm1/ABCDEF/brightness/set",
            "openm1/ABCDEF/state","value_json.brightness","\"min\":0","\"max\":4","\"step\":1"};
        assert(strstr(payloads[4],required[i]));
    }
    assert(strstr(payloads[4],"\"mode\":\"slider\""));
    assert(strstr(payloads[4],"openm1/ABCDEF/availability"));
    assert(strstr(payloads[4],"OpenM1 v0.6.13"));
    publish_count=0;
    assert(homeassistant_publish(&client,&config,1)==0 && publish_count==5);
    for (i=0;i<5;i++) assert(lengths[i]==0 && payloads[i][0]==0);
    puts("PASS five retained HA discoveries and removal");
    return 0;
}
