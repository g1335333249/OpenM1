#include "m1_sensor.h"
#include <stdio.h>
#include <string.h>

static mico_mutex_t sensor_mutex;
static m1_sensor_snapshot_t sensor;
static int sensor_ready;
static uint8_t frame[20];
static unsigned frame_used;

static void accept_frame(const uint8_t f[20])
{
    unsigned hcho=(unsigned)f[2]*256u+f[3];
    unsigned pm25=(unsigned)f[9]*256u+f[10];
    float temperature=(float)f[5]+(float)(f[6]/10u)/10.0f;
    float humidity=(float)f[7]+(float)(f[8]/10u)/10.0f;
    float formaldehyde=(float)hcho/1000.0f;
    int tv=f[6]<=99 && temperature>=-40.0f && temperature<=100.0f;
    int hv=f[8]<=99 && humidity<=100.0f;
    int pv=pm25<=2000;
    int fv=formaldehyde<=10.0f;
    mico_rtos_lock_mutex(&sensor_mutex);
    sensor.frame_count++;
    sensor.sensor_frames++;
    sensor.temperature=temperature; sensor.temperature_valid=tv!=0;
    sensor.humidity=humidity; sensor.humidity_valid=hv!=0;
    sensor.pm25=(uint16_t)pm25; sensor.pm25_valid=pv!=0;
    sensor.formaldehyde=formaldehyde; sensor.formaldehyde_valid=fv!=0;
    if (tv||hv||pv||fv) sensor.last_update_ms=mico_rtos_get_time();
    if (!tv||!hv||!pv||!fv) {
        sensor.parser_error_count++;
        sensor.invalid_frames++;
    }
    mico_rtos_unlock_mutex(&sensor_mutex);
}

static void accept_time_frame(const uint8_t f[20])
{
    unsigned year=(unsigned)f[2]*256u+f[3];
    int sane=year>=2000 && year<=2099 && f[4]>=1 && f[4]<=12 &&
             f[5]>=1 && f[5]<=31 && f[6]<=23 && f[7]<=59 && f[8]<=59;
    mico_rtos_lock_mutex(&sensor_mutex);
    sensor.time_frames++;
    if (sane)
        snprintf(sensor.m1_datetime,sizeof(sensor.m1_datetime),
                 "%04u-%02u-%02u %02u:%02u:%02u",year,
                 (unsigned)f[4],(unsigned)f[5],(unsigned)f[6],
                 (unsigned)f[7],(unsigned)f[8]);
    else { sensor.invalid_frames++; sensor.parser_error_count++; }
    mico_rtos_unlock_mutex(&sensor_mutex);
}

OSStatus m1_sensor_init(void)
{
    OSStatus err=mico_rtos_init_mutex(&sensor_mutex);
    if (err==kNoErr) sensor_ready=1;
    return err;
}

/* Reference zM1 uses 20-byte '#' ... '!' packets. Type 1 contains sensor
 * fields. No checksum was found in its parser, so this remains PARTIAL until
 * real UART bytes and the M1 display confirm the values. */
void m1_sensor_parse(const uint8_t *bytes, size_t length)
{
    size_t i;
    if (!sensor_ready || !bytes || !length) return;
    mico_rtos_lock_mutex(&sensor_mutex);
    sensor.raw_bytes_seen+=(uint32_t)length;
    mico_rtos_unlock_mutex(&sensor_mutex);
    for (i=0;i<length;i++) {
        if (!frame_used && bytes[i]!='#') continue;
        frame[frame_used++]=bytes[i];
        if (frame_used==sizeof(frame)) {
            if (frame[19]=='!') {
                mico_rtos_lock_mutex(&sensor_mutex);
                sensor.total_frames++;
                mico_rtos_unlock_mutex(&sensor_mutex);
                switch (frame[1]) {
                case 0x01: accept_frame(frame); break;
                case 0x0c: accept_time_frame(frame); break;
                case 0x0f:
                    mico_rtos_lock_mutex(&sensor_mutex);
                    sensor.type0f_frames++;
                    sensor.type0f_last_value=frame[2];
                    sensor.type0f_last_rx_ms=mico_rtos_get_time();
                    mico_rtos_unlock_mutex(&sensor_mutex);
                    break;
                case 0x18:
                    mico_rtos_lock_mutex(&sensor_mutex);
                    sensor.type18_frames++;
                    mico_rtos_unlock_mutex(&sensor_mutex);
                    break;
                default:
                    mico_rtos_lock_mutex(&sensor_mutex);
                    sensor.unknown_frames++;
                    mico_rtos_unlock_mutex(&sensor_mutex);
                    break;
                }
                frame_used=0;
            } else {
                unsigned j;
                mico_rtos_lock_mutex(&sensor_mutex);
                sensor.parser_error_count++;
                sensor.invalid_frames++;
                mico_rtos_unlock_mutex(&sensor_mutex);
                /* Preserve a possible later start marker in a damaged packet. */
                for (j=1;j<sizeof(frame) && frame[j]!='#';j++) {}
                if (j<sizeof(frame)) {
                    frame_used=sizeof(frame)-j;
                    memmove(frame,frame+j,frame_used);
                } else frame_used=0;
            }
        }
    }
}

void m1_sensor_get_snapshot(m1_sensor_snapshot_t *out)
{
    if (!out) return;
    memset(out,0,sizeof(*out));
    if (!sensor_ready) return;
    mico_rtos_lock_mutex(&sensor_mutex);
    *out=sensor;
    mico_rtos_unlock_mutex(&sensor_mutex);
}

void m1_sensor_json(char *out, size_t capacity)
{
    m1_sensor_snapshot_t s;
    char t[24],h[24],p[24],f[24];
    uint32_t age;
    m1_sensor_get_snapshot(&s);
    if (s.temperature_valid) snprintf(t,sizeof(t),"%.1f",(double)s.temperature);
    else snprintf(t,sizeof(t),"null");
    if (s.humidity_valid) snprintf(h,sizeof(h),"%.1f",(double)s.humidity);
    else snprintf(h,sizeof(h),"null");
    if (s.pm25_valid) snprintf(p,sizeof(p),"%u",(unsigned)s.pm25);
    else snprintf(p,sizeof(p),"null");
    if (s.formaldehyde_valid) snprintf(f,sizeof(f),"%.3f",(double)s.formaldehyde);
    else snprintf(f,sizeof(f),"null");
    age=s.last_update_ms ? mico_rtos_get_time()-s.last_update_ms : 0;
    snprintf(out,capacity,
             "{\"temperature\":%s,\"humidity\":%s,\"PM25\":%s,\"formaldehyde\":%s,"
             "\"temperature_valid\":%s,\"humidity_valid\":%s,\"PM25_valid\":%s,\"formaldehyde_valid\":%s,"
             "\"age_ms\":%lu,\"frame_count\":%lu,\"parser_error_count\":%lu,\"raw_bytes_seen\":%lu,\"protocol_status\":\"partial\"}",
             t,h,p,f,s.temperature_valid?"true":"false",s.humidity_valid?"true":"false",
             s.pm25_valid?"true":"false",s.formaldehyde_valid?"true":"false",
             (unsigned long)age,(unsigned long)s.frame_count,(unsigned long)s.parser_error_count,
             (unsigned long)s.raw_bytes_seen);
}
