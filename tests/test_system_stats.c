#include "system_stats.h"
#include "mico_hal/mico_flash.h"
#include <assert.h>
#include <string.h>

static micoMemInfo_t mem={3,100000,25000,75000};
static mico_logic_partition_t ota={0x110000,0xB5000};
micoMemInfo_t *MicoGetMemoryInfo(void) { return &mem; }
mico_logic_partition_t *MicoFlashGetInfo(mico_partition_t p) { return p==MICO_PARTITION_OTA_TEMP?&ota:0; }
OSStatus mico_rtos_init_mutex(mico_mutex_t *m) { *m=1; return 0; }
OSStatus mico_rtos_lock_mutex(mico_mutex_t *m) { assert(*m); return 0; }
OSStatus mico_rtos_unlock_mutex(mico_mutex_t *m) { assert(*m); return 0; }
uint32_t mico_rtos_get_time(void) { return 0; }
void mico_thread_msleep(uint32_t ms) { (void)ms; }
OSStatus mico_rtos_create_thread(mico_thread_t *t,uint8_t p,const char *n,
                                 mico_thread_function_t f,uint32_t s,mico_thread_arg_t a)
{ (void)t;(void)p;(void)n;(void)f;(void)s;(void)a; return -1; }
int main(void)
{
    char out[1280],tiny[12];
    assert(system_stats_cpu_estimate(100,100)==0);
    assert(system_stats_cpu_estimate(50,100)==50);
    assert(system_stats_cpu_estimate(0,100)==100);
    assert(system_stats_cpu_estimate(200,100)==0);
    assert(system_stats_cpu_estimate(0,0)==0);
    system_stats_json(out,sizeof(out));
    assert(strstr(out,"\"ready\":false") && strstr(out,"\"usage_percent\":null"));
    assert(strstr(out,"\"total_bytes\":100000") && strstr(out,"\"used_percent\":25"));
    assert(strstr(out,"\"start\":1114112") && strstr(out,"\"length\":741376"));
    system_stats_json(tiny,sizeof(tiny));
    assert(tiny[sizeof(tiny)-1]==0 || tiny[0]=='{');
    return 0;
}
