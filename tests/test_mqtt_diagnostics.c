#include "mqtt_diagnostics.h"
#include "mqtt_bounded_read.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    assert(strcmp(mqtt_failure_stage_name(MQTT_STAGE_MQTT_CONNECT),"mqtt_connect")==0);
    assert(strcmp(mqtt_failure_stage_name(MQTT_STAGE_AVAILABILITY_PUBLISH),"availability_publish")==0);
    assert(strcmp(mqtt_failure_stage_name(MQTT_STAGE_HA_DISCOVERY),"ha_discovery")==0);
    assert(strcmp(mqtt_failure_stage_name(MQTT_STAGE_STATE_PUBLISH),"state_publish")==0);
    assert(mqtt_failure_is_publish(MQTT_STAGE_AVAILABILITY_PUBLISH));
    assert(mqtt_failure_is_publish(MQTT_STAGE_HA_DISCOVERY));
    assert(mqtt_failure_is_publish(MQTT_STAGE_STATE_PUBLISH));
    assert(mqtt_yield_failure_stage(0,0)==MQTT_STAGE_YIELD);
    assert(mqtt_yield_failure_stage(OPENM1_MQTT_READ_PEER_CLOSED,0)==MQTT_STAGE_PEER_CLOSED);
    assert(mqtt_yield_failure_stage(OPENM1_MQTT_READ_SOCKET_ERROR,0)==MQTT_STAGE_SOCKET_READ);
    assert(mqtt_yield_failure_stage(0,-1)==MQTT_STAGE_SOCKET_WRITE);
    assert(mqtt_failure_is_yield(MQTT_STAGE_YIELD));
    assert(strcmp(mqtt_failure_stage_name(MQTT_STAGE_CONFIG_CHANGED),"config_changed")==0);
    assert(strcmp(mqtt_failure_stage_name(MQTT_STAGE_WIFI_LOST),"wifi_lost")==0);
    puts("PASS MQTT failure stage classification");
    return 0;
}
