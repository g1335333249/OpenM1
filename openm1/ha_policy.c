#include "ha_policy.h"
int ha_policy_can_enable(int mqtt_configured,int mqtt_enabled,int mqtt_connected)
{
    return mqtt_configured && mqtt_enabled && mqtt_connected;
}
