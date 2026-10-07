#pragma once
#include <stddef.h>

typedef struct Client {int unused;} Client;
enum QoS {QOS0,QOS1,QOS2};
typedef struct MQTTMessage {
    int qos;
    char retained;
    char dup;
    unsigned short id;
    void *payload;
    size_t payloadlen;
} MQTTMessage;
#define MQTT_SUCCESS 0
int MQTTPublish(Client *client,const char *topic,MQTTMessage *message);
