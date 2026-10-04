#include "m1_sensor.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t test_time=12345;
OSStatus mico_rtos_init_mutex(mico_mutex_t *mutex) { *mutex=1; return kNoErr; }
OSStatus mico_rtos_lock_mutex(mico_mutex_t *mutex) { assert(*mutex); return kNoErr; }
OSStatus mico_rtos_unlock_mutex(mico_mutex_t *mutex) { assert(*mutex); return kNoErr; }
uint32_t mico_rtos_get_time(void) { return test_time; }

int main(void)
{
    m1_sensor_snapshot_t s;
    char json[512];
    const uint8_t frame[20]={'#',1,0,36,0,25,60,48,20,0,12,0,0,0,0,0,0,0,0,'!'};
    uint8_t damaged[20];
    assert(m1_sensor_init()==kNoErr);
    m1_sensor_json(json,sizeof(json));
    assert(strstr(json,"\"temperature\":null")!=NULL);
    assert(strstr(json,"\"temperature_valid\":false")!=NULL);
    m1_sensor_parse(frame,7);
    m1_sensor_get_snapshot(&s);
    assert(s.frame_count==0);
    m1_sensor_parse(frame+7,13);
    m1_sensor_get_snapshot(&s);
    assert(s.frame_count==1 && s.parser_error_count==0);
    assert(s.temperature==25.6f && s.humidity==48.2f);
    assert(s.pm25==12 && s.formaldehyde>0.035f && s.formaldehyde<0.037f);
    assert(s.temperature_valid && s.humidity_valid && s.pm25_valid && s.formaldehyde_valid);
    m1_sensor_json(json,sizeof(json));
    assert(strstr(json,"\"PM25\":12")!=NULL);
    assert(strstr(json,"\"formaldehyde\":0.036")!=NULL);
    memcpy(damaged,frame,sizeof(damaged)); damaged[19]=0;
    m1_sensor_parse(damaged,sizeof(damaged));
    m1_sensor_get_snapshot(&s);
    assert(s.parser_error_count==1 && s.frame_count==1);
    m1_sensor_parse(frame,sizeof(frame));
    m1_sensor_get_snapshot(&s);
    assert(s.frame_count==2);
    puts("M1_SENSOR_PARSER_PASS");
    return 0;
}
