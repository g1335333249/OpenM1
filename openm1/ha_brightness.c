#include "ha_brightness.h"
#include <stdio.h>

int ha_brightness_parse(const void *payload,size_t length,uint8_t *level)
{
    const unsigned char *bytes=(const unsigned char *)payload;
    if (!bytes || !level || length!=1 || bytes[0]<'0' || bytes[0]>'4') return 0;
    *level=(uint8_t)(bytes[0]-'0');
    return 1;
}

void ha_brightness_enqueue(volatile ha_brightness_pending_t *pending,const void *payload,size_t length)
{
    uint8_t level;
    if (!pending) return;
    if (!ha_brightness_parse(payload,length,&level)) {pending->invalid=1;return;}
    pending->level=level;
    pending->pending=1;
}

int ha_brightness_take(volatile ha_brightness_pending_t *pending,int ota_busy,uint8_t *level)
{
    if (!pending || !level || !pending->pending) return 0;
    pending->pending=0;
    if (ota_busy) return 0; /* Never replay a pre-OTA command later. */
    *level=pending->level;
    return 1;
}

int ha_brightness_take_invalid(volatile ha_brightness_pending_t *pending)
{
    int invalid;
    if (!pending) return 0;
    invalid=pending->invalid;
    pending->invalid=0;
    return invalid;
}

int ha_brightness_format_state(char *out,size_t capacity,
    const char *temperature,const char *humidity,const char *pm25,const char *formaldehyde,
    uint8_t brightness,uint32_t uptime_seconds,int rssi)
{
    int n;
    if (!out || !capacity || brightness>4) return -1;
    n=snprintf(out,capacity,
        "{\"temperature\":%s,\"humidity\":%s,\"PM25\":%s,"
        "\"formaldehyde\":%s,\"brightness\":%u,\"uptime\":%lu,\"rssi\":%d}",
        temperature,humidity,pm25,formaldehyde,(unsigned)brightness,
        (unsigned long)uptime_seconds,rssi);
    return n<0 || (size_t)n>=capacity?-1:0;
}
