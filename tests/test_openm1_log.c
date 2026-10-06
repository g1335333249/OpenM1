#include "openm1_log.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t fake_time;
OSStatus mico_rtos_init_mutex(mico_mutex_t *mutex) { *mutex=1; return kNoErr; }
OSStatus mico_rtos_lock_mutex(mico_mutex_t *mutex) { assert(*mutex==1); return kNoErr; }
OSStatus mico_rtos_unlock_mutex(mico_mutex_t *mutex) { assert(*mutex==1); return kNoErr; }
uint32_t mico_rtos_get_time(void) { return ++fake_time; }
int main(void)
{
    openm1_log_status_t status;
    openm1_log_record_t rec;
    char json[2304];
    unsigned i;
    assert(openm1_log_ram_bytes()<=2560);
    assert(openm1_log_init()==kNoErr);
    openm1_log_info("BOOT","first");
    openm1_log_warn("WIFI","quote \" and slash \\ and newline \n");
    openm1_log_error("OTA","%s","failure");
    openm1_log_status(&status);
    assert(status.count==3 && status.next_sequence==4);
    openm1_log_json(1,json,sizeof(json));
    assert(strstr(json,"\"seq\":2") && !strstr(json,"\"seq\":1"));
    assert(strstr(json,"\\\"") && strstr(json,"\\\\"));
    assert(strstr(json,"\\u000a"));
    assert(strstr(json,"\"level\":\"WARN\"") && strstr(json,"\"level\":\"ERROR\""));
    for (i=0;i<25;i++) openm1_log_info("SYSTEM","%u",i);
    openm1_log_status(&status);
    assert(status.count==24 && status.oldest_sequence==5 && status.newest_sequence==28);
    assert(status.dropped_count==4 && status.wrap_count>=1);
    assert(!openm1_log_get_record(1,&rec));
    assert(openm1_log_get_record(28,&rec) && strcmp(rec.message,"24")==0);
    openm1_log_json(1,json,sizeof(json));
    assert(strstr(json,"\"cursor_reset\":true") && strstr(json,"\"seq\":5"));
    openm1_log_info("SYSTEM","%s","abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789LONG");
    assert(openm1_log_get_record(29,&rec) && strlen(rec.message)==63);
    openm1_log_clear();
    openm1_log_status(&status);
    assert(status.count==0 && status.next_sequence==30);
    openm1_log_json(29,json,sizeof(json));
    assert(strstr(json,"\"records\":[]"));
    puts("PASS RAM log ring/cursor/JSON/clear");
    return 0;
}
