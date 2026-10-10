#include "homeassistant.h"
#include "wifi_manager.h"
#include "recovery.h"
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *object;
    const char *name;
    const char *unit;
    const char *device_class;
    const char *json_key;
} ha_entity_t;
static const ha_entity_t entities[4]={
    {"temperature","温度","°C","temperature","temperature"},
    {"humidity","湿度","%","humidity","humidity"},
    {"pm25","PM2.5","µg/m³","pm25","PM25"},
    {"formaldehyde","甲醛","mg/m³","","formaldehyde"}
};

int homeassistant_publish(Client *client,const openm1_config_t *config,int remove)
{
    const char *suffix=strrchr(recovery_ssid(),'-');
    char id[24],topic[160],state[150],availability[160],ip[16],url[48],payload[800],class_field[64];
    MQTTMessage message;
    unsigned i;
    int n;
    suffix=suffix?suffix+1:"RECOVERY";
    n=snprintf(id,sizeof(id),"openm1_%s",suffix);
    if (n<0 || (size_t)n>=sizeof(id)) return -1;
    n=snprintf(state,sizeof(state),"%s/state",config->base_topic);
    if (n<0 || (size_t)n>=sizeof(state)) return -1;
    n=snprintf(availability,sizeof(availability),"%s/availability",config->base_topic);
    if (n<0 || (size_t)n>=sizeof(availability)) return -1;
    wifi_manager_station_ip(ip);
    snprintf(url,sizeof(url),"http://%s/",ip[0]?ip:RECOVERY_IP);
    memset(&message,0,sizeof(message));
    message.qos=QOS0; message.retained=1;
    for (i=0;i<4;i++) {
        const ha_entity_t *entity=&entities[i];
        n=snprintf(topic,sizeof(topic),"%s/sensor/%s/%s/config",config->discovery_prefix,id,entity->object);
        if (n<0 || (size_t)n>=sizeof(topic)) return -1;
        if (remove) payload[0]=0;
        else {
            if (entity->device_class[0])
                snprintf(class_field,sizeof(class_field),"\"device_class\":\"%s\",",entity->device_class);
            else class_field[0]=0;
            n=snprintf(payload,sizeof(payload),
                "{\"name\":\"%s\",\"unique_id\":\"%s_%s\",\"state_topic\":\"%s\","
                "\"value_template\":\"{{ value_json.%s }}\",\"unit_of_measurement\":\"%s\","
                "\"state_class\":\"measurement\",%s"
                "\"availability_topic\":\"%s\",\"payload_available\":\"online\",\"payload_not_available\":\"offline\","
                "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\",\"manufacturer\":\"Phicomm / OpenM1\","
                "\"model\":\"M1\",\"sw_version\":\"OpenM1 v0.6.16\",\"configuration_url\":\"%s\"}}",
                entity->name,id,entity->object,state,entity->json_key,entity->unit,
                class_field,
                availability,id,recovery_ssid(),url);
            if (n<0 || (size_t)n>=sizeof(payload)) return -1;
        }
        message.payload=payload;
        message.payloadlen=strlen(payload);
        if (MQTTPublish(client,topic,&message)!=MQTT_SUCCESS) return -1;
    }
    n=snprintf(topic,sizeof(topic),"%s/number/%s/brightness/config",config->discovery_prefix,id);
    if (n<0 || (size_t)n>=sizeof(topic)) return -1;
    if (remove) payload[0]=0;
    else {
        char command[160];
        n=snprintf(command,sizeof(command),"%s/brightness/set",config->base_topic);
        if (n<0 || (size_t)n>=sizeof(command)) return -1;
        n=snprintf(payload,sizeof(payload),
            "{\"name\":\"屏幕亮度\",\"unique_id\":\"%s_brightness\","
            "\"command_topic\":\"%s\",\"state_topic\":\"%s\","
            "\"value_template\":\"{{ value_json.brightness }}\","
            "\"min\":0,\"max\":4,\"step\":1,\"mode\":\"slider\","
            "\"availability_topic\":\"%s\",\"payload_available\":\"online\","
            "\"payload_not_available\":\"offline\","
            "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
            "\"manufacturer\":\"Phicomm / OpenM1\",\"model\":\"M1\","
            "\"sw_version\":\"OpenM1 v0.6.16\",\"configuration_url\":\"%s\"}}",
            id,command,state,availability,id,recovery_ssid(),url);
        if (n<0 || (size_t)n>=sizeof(payload)) return -1;
    }
    message.payload=payload;
    message.payloadlen=strlen(payload);
    if (MQTTPublish(client,topic,&message)!=MQTT_SUCCESS) return -1;
    return 0;
}
