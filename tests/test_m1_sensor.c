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
    /* Synthetic type 0x01 frame: not a captured M1 sensor sample. */
    const uint8_t frame[20]={'#',1,0,36,0,25,60,48,20,0,12,0,0,0,0,0,0,0,0,'!'};
    const uint8_t time_frames[4][20]={
        {'#',0x0c,0x07,0xea,0x0a,0x05,0x07,0x00,0x2f,0,0,0,0,0,0,0,0,0,0,'!'},
        {'#',0x0c,0x07,0xea,0x0a,0x05,0x07,0x01,0x2d,0,0,0,0,0,0,0,0,0,0,'!'},
        {'#',0x0c,0x07,0xea,0x0a,0x05,0x07,0x02,0x2b,0,0,0,0,0,0,0,0,0,0,'!'},
        {'#',0x0c,0x07,0xea,0x0a,0x05,0x07,0x03,0x28,0,0,0,0,0,0,0,0,0,0,'!'}
    };
    const uint8_t type0f_zero[20]={'#',0x0f,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'!'};
    const uint8_t type0f_one[20]={'#',0x0f,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'!'};
    const uint8_t type18[20]={'#',0x18,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'!'};
    const uint8_t unknown[20]={'#',0x77,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,'!'};
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
    m1_sensor_parse(time_frames[0],9);
    m1_sensor_parse(time_frames[0]+9,11);
    m1_sensor_parse(time_frames[1],20);
    m1_sensor_parse(time_frames[2],20);
    m1_sensor_parse(time_frames[3],20);
    m1_sensor_parse(type0f_zero,sizeof(type0f_zero));
    test_time=13000;
    m1_sensor_parse(type0f_one,sizeof(type0f_one));
    m1_sensor_parse(type18,sizeof(type18));
    m1_sensor_parse(unknown,sizeof(unknown));
    m1_sensor_get_snapshot(&s);
    assert(s.total_frames==10 && s.sensor_frames==2 && s.time_frames==4);
    assert(s.type0f_frames==2 && s.type18_frames==1 && s.unknown_frames==1);
    assert(s.invalid_frames==1 && s.type0f_last_value==1 && s.type0f_last_rx_ms==13000);
    assert(strcmp(s.m1_datetime,"2026-10-05 07:03:40")==0);
    {
        uint8_t stream[43] = {0x11,0x22,0x33};
        memcpy(stream+3,frame,20);
        memcpy(stream+23,frame,20);
        m1_sensor_parse(stream,sizeof(stream)); /* bad prefix and two sticky frames */
        m1_sensor_get_snapshot(&s);
        assert(s.total_frames==12 && s.sensor_frames==4);
    }
    puts("M1_SENSOR_PARSER_PASS");
    return 0;
}
