#include "system_stats.h"
#include "recovery.h"
#include "mico_hal/mico_flash.h"
#include <assert.h>
#include <string.h>

static micoMemInfo_t mem={3,100000,25000,75000};
static int create_count;
static void (*overflow_callback)(char *,void *);
static mico_logic_partition_t ota={0x110000,0xB5000};
micoMemInfo_t *MicoGetMemoryInfo(void) { return &mem; }
mico_logic_partition_t *MicoFlashGetInfo(mico_partition_t p) { return p==MICO_PARTITION_OTA_TEMP?&ota:0; }
OSStatus mico_rtos_init_mutex(mico_mutex_t *m) { *m=1; return 0; }
OSStatus mico_rtos_lock_mutex(mico_mutex_t *m) { assert(*m); return 0; }
OSStatus mico_rtos_unlock_mutex(mico_mutex_t *m) { assert(*m); return 0; }
uint32_t mico_rtos_get_time(void) { return 0; }
void mico_thread_msleep(uint32_t ms) { (void)ms; }
int wifi_manager_control_running(void) { return 0; }
int recovery_ota_busy(void) { return 0; }
OSStatus mico_system_notify_register(mico_notify_types_t type, void *handler, void *arg)
{ (void)type; (void)arg; overflow_callback=(void (*)(char *,void *))handler; return 0; }
OSStatus mico_rtos_create_thread(mico_thread_t *t,uint8_t p,const char *n,
                                 mico_thread_function_t f,uint32_t s,mico_thread_arg_t a)
{ (void)t;(void)p;(void)n;(void)f;(void)s;(void)a; create_count++; return -1; }
int main(void)
{
    char out[1280],tiny[12];
    assert(system_stats_cpu_estimate(100,100)==0);
    assert(system_stats_cpu_estimate(50,100)==50);
    assert(system_stats_cpu_estimate(0,100)==100);
    assert(system_stats_cpu_estimate(200,100)==0);
    assert(system_stats_cpu_estimate(0,0)==0);
    assert(system_stats_register_stack_diagnostic()==0);
    assert(system_stats_init()==0 && create_count==0); /* CPU is lazy. */
    system_stats_note_heap(1);
    assert(system_stats_begin_optional_thread(SYSTEM_STATS_CPU_STACK));
    system_stats_end_thread_creation();
    assert(!system_stats_low_memory_safe_mode());
    overflow_callback("test",0);
    assert(system_stats_stack_overflow_count()==1);
    system_stats_json(out,sizeof(out));
    assert(strstr(out,"\"ready\":false") && strstr(out,"\"usage_percent\":null"));
    assert(strstr(out,"\"total_bytes\":100000") && strstr(out,"\"used_percent\":25"));
    assert(strstr(out,"\"start\":1114112") && strstr(out,"\"length\":741376"));
    assert(strstr(out,"\"boot_min_free_bytes\":75000"));
    assert(strstr(out,"\"runtime_min_free_bytes\":75000"));
    assert(strstr(out,"\"stack_overflow_count\":1"));
    system_stats_json(tiny,sizeof(tiny));
    assert(tiny[sizeof(tiny)-1]==0 || tiny[0]=='{');
    mem.free_memory=RECOVERY_OTA_STACK+OPENM1_OTA_HEAP_RESERVE;
    assert(!system_stats_begin_ota_thread(RECOVERY_OTA_STACK));
    mem.free_memory++;
    assert(system_stats_begin_ota_thread(RECOVERY_OTA_STACK));
    system_stats_end_thread_creation();
    return 0;
}
