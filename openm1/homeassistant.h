#pragma once
#include "config_store.h"
#include "MQTTClient.h"

/* Called only by the MQTT worker; it owns the MQTT Client. */
int homeassistant_publish(Client *client,const openm1_config_t *config,int remove);
