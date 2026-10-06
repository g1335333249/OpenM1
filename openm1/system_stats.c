#include "system_stats.h"
#include "openm1_log.h"
#include "wifi_station_logic.h"
#include "mico_hal/mico_flash.h"
#include "recovery.h"
#include "wifi_manager.h"
#include <stdio.h>
#include <string.h>

static mico_mutex_t stats_mutex;
static mico_mutex_t thread_creation_mutex;
static mico_thread_t cpu_thread;
static int stats_ready,thread_creation_ready,cpu_sample_ready,cpu_thread_started,low_memory_safe_mode;
static int current_free_heap,boot_min_free_heap,runtime_min_free_heap;
static char boot_phase[16]="recovery";
static volatile uint32_t stack_overflow_count;
static unsigned cpu_usage_percent;
static uint32_t max_idle_iterations;

static void stack_overflow_notice(char *taskname,void *arg)
{
    (void)taskname; (void)arg;
    stack_overflow_count++; /* Notification context: no printf, mutex or allocation. */
}
OSStatus system_stats_register_stack_diagnostic(void)
{
    return mico_system_notify_register(mico_notify_Stack_Overflow_ERROR,
                                       (void *)stack_overflow_notice,NULL);
}
uint32_t system_stats_stack_overflow_count(void) { return stack_overflow_count; }
int system_stats_free_heap(void)
{
    micoMemInfo_t *memory=MicoGetMemoryInfo();
    return memory?memory->free_memory:0;
}
void system_stats_note_heap(int boot_sample)
{
    int free_bytes=system_stats_free_heap();
    if (stats_ready) mico_rtos_lock_mutex(&stats_mutex);
    current_free_heap=free_bytes;
    if (!runtime_min_free_heap || free_bytes<runtime_min_free_heap) runtime_min_free_heap=free_bytes;
    if (boot_sample && (!boot_min_free_heap || free_bytes<boot_min_free_heap))
        boot_min_free_heap=free_bytes;
    if (free_bytes<(int)OPENM1_MIN_HEAP_RESERVE) {
        low_memory_safe_mode=1;
        snprintf(boot_phase,sizeof(boot_phase),"safe_mode");
    }
    if (stats_ready) mico_rtos_unlock_mutex(&stats_mutex);
}
static int begin_thread_creation(unsigned stack_bytes,unsigned reserve,int optional)
{
    int available;
    if (optional && low_memory_safe_mode) return 0;
    if (thread_creation_ready) mico_rtos_lock_mutex(&thread_creation_mutex);
    system_stats_note_heap(0);
    available=current_free_heap>0 &&
              (unsigned)current_free_heap>stack_bytes+reserve &&
              (!optional || !low_memory_safe_mode);
    if (!available && thread_creation_ready) mico_rtos_unlock_mutex(&thread_creation_mutex);
    return available;
}
int system_stats_begin_optional_thread(unsigned stack_bytes)
{
    unsigned reserve=RECOVERY_OTA_STACK+OPENM1_OTA_HEAP_RESERVE;
    if (reserve<OPENM1_MIN_HEAP_RESERVE) reserve=OPENM1_MIN_HEAP_RESERVE;
    return begin_thread_creation(stack_bytes,reserve,1);
}
int system_stats_begin_ota_thread(unsigned stack_bytes)
{
    return begin_thread_creation(stack_bytes,OPENM1_OTA_HEAP_RESERVE,0);
}
void system_stats_end_thread_creation(void)
{
    system_stats_note_heap(0);
    if (thread_creation_ready) mico_rtos_unlock_mutex(&thread_creation_mutex);
}
int system_stats_low_memory_safe_mode(void) { return low_memory_safe_mode; }
void system_stats_enter_safe_mode(void)
{
    low_memory_safe_mode=1;
    snprintf(boot_phase,sizeof(boot_phase),"safe_mode");
}
void system_stats_set_boot_phase(const char *phase)
{
    if (!low_memory_safe_mode && phase)
        snprintf(boot_phase,sizeof(boot_phase),"%s",phase);
}

unsigned system_stats_cpu_estimate(uint32_t current,uint32_t baseline)
{
    uint64_t idle_percent;
    if (!baseline) return 0;
    idle_percent=(uint64_t)current*100u/baseline;
    if (idle_percent>100u) idle_percent=100u;
    return (unsigned)(100u-idle_percent);
}

static void cpu_sampler(mico_thread_arg_t arg)
{
    volatile uint32_t iterations;
    uint32_t start;
    unsigned sample;
    (void)arg;
    for (;;) {
        iterations=0;
        start=mico_rtos_get_time();
        while ((uint32_t)(mico_rtos_get_time()-start)<SYSTEM_STATS_CPU_SAMPLE_MS)
            iterations++;
        mico_rtos_lock_mutex(&stats_mutex);
        if (iterations>max_idle_iterations) max_idle_iterations=iterations;
        sample=system_stats_cpu_estimate(iterations,max_idle_iterations);
        cpu_usage_percent=cpu_sample_ready?(cpu_usage_percent*3u+sample)/4u:sample;
        cpu_sample_ready=1;
        mico_rtos_unlock_mutex(&stats_mutex);
        mico_thread_msleep(SYSTEM_STATS_CPU_INTERVAL_MS-SYSTEM_STATS_CPU_SAMPLE_MS);
    }
}

OSStatus system_stats_init(void)
{
    OSStatus err=mico_rtos_init_mutex(&stats_mutex);
    if (err!=kNoErr) return err;
    err=mico_rtos_init_mutex(&thread_creation_mutex);
    if (err!=kNoErr) return err;
    thread_creation_ready=1;
    stats_ready=1;
    system_stats_note_heap(1);
    return kNoErr;
}
void system_stats_maybe_start_cpu(void)
{
    OSStatus err;
    if (!stats_ready || cpu_thread_started || recovery_ota_busy() ||
        !system_stats_begin_optional_thread(SYSTEM_STATS_CPU_STACK)) return;
    err=mico_rtos_create_thread(&cpu_thread,SYSTEM_STATS_CPU_PRIORITY,
                               "openm1_cpu_sample",cpu_sampler,SYSTEM_STATS_CPU_STACK,0);
    if (err==kNoErr) cpu_thread_started=1;
    else printf("STATS: CPU sampler unavailable: %d\r\n",err);
    system_stats_end_thread_creation();
}

void system_stats_json(char *out,size_t capacity)
{
    openm1_log_status_t log_status;
    static const struct { mico_partition_t id; const char *name; } partitions[]={
        {MICO_PARTITION_BOOTLOADER,"bootloader"},
        {MICO_PARTITION_APPLICATION,"application"},
        {MICO_PARTITION_ATE,"ate"},
        {MICO_PARTITION_OTA_TEMP,"ota_temp"},
        {MICO_PARTITION_RF_FIRMWARE,"rf_firmware"},
        {MICO_PARTITION_PARAMETER_1,"parameter_1"},
        {MICO_PARTITION_PARAMETER_2,"parameter_2"},
        {MICO_PARTITION_USER,"user"},
        {MICO_PARTITION_SDS,"sds"}
    };
    micoMemInfo_t *memory=MicoGetMemoryInfo();
    unsigned usage=0,used_percent=0;
    char cpu_value[16];
    int ready=0,total=0,allocated=0,free_bytes=0,chunks=0;
    int boot_min,runtime_min,safe,stable;
    char phase[16];
    size_t i,used=0;
    int n,count=0;
    if (!out || !capacity) return;
    openm1_log_status(&log_status);
    system_stats_note_heap(0);
    if (stats_ready) {
        mico_rtos_lock_mutex(&stats_mutex);
        usage=cpu_usage_percent; ready=cpu_sample_ready;
        boot_min=boot_min_free_heap; runtime_min=runtime_min_free_heap;
        safe=low_memory_safe_mode;
        snprintf(phase,sizeof(phase),"%s",boot_phase);
        mico_rtos_unlock_mutex(&stats_mutex);
    } else { boot_min=boot_min_free_heap; runtime_min=runtime_min_free_heap;
             safe=low_memory_safe_mode; snprintf(phase,sizeof(phase),"%s",boot_phase); }
    stable=!safe && mico_rtos_get_time()>=WIFI_BOOT_AUTO_CONNECT_GRACE_MS &&
           wifi_manager_control_running() &&
           !stack_overflow_count && system_stats_free_heap()>=(int)OPENM1_MIN_HEAP_RESERVE;
    if (stable) snprintf(phase,sizeof(phase),"stable");
    if (memory) {
        total=memory->total_memory>0?memory->total_memory:0;
        allocated=memory->allocted_memory>0?memory->allocted_memory:0;
        free_bytes=memory->free_memory>0?memory->free_memory:0;
        chunks=memory->num_of_chunks>0?memory->num_of_chunks:0;
        if (total) used_percent=(unsigned)((uint64_t)allocated*100u/(unsigned)total);
        if (used_percent>100) used_percent=100;
    }
    if (ready) snprintf(cpu_value,sizeof(cpu_value),"%u",usage);
    else strcpy(cpu_value,"null");
    n=snprintf(out,capacity,
       "{\"cpu\":{\"usage_percent\":%s,\"estimated\":true,\"sample_window_ms\":100,"
       "\"sample_interval_ms\":5000,\"ready\":%s},"
       "\"memory\":{\"total_bytes\":%d,\"allocated_bytes\":%d,\"free_bytes\":%d,"
       "\"free_chunks\":%d,\"used_percent\":%u,\"boot_min_free_bytes\":%d,"
       "\"runtime_min_free_bytes\":%d},\"boot_phase\":\"%s\",\"boot_stable\":%s,"
       "\"low_memory_safe_mode\":%s,\"stack_overflow_count\":%lu,"
       "\"last_stack_overflow_task\":\"unknown\","
       "\"flash\":{\"filesystem\":false,\"type\":\"raw_partitions\",\"partitions\":[",
       cpu_value,ready?"true":"false",total,allocated,free_bytes,chunks,used_percent,
       boot_min,runtime_min,phase,stable?"true":"false",safe?"true":"false",
       (unsigned long)stack_overflow_count);
    if (n<0 || (size_t)n>=capacity) goto overflow;
    used=(size_t)n;
    for (i=0;i<sizeof(partitions)/sizeof(partitions[0]);i++) {
        mico_logic_partition_t *p=MicoFlashGetInfo(partitions[i].id);
        if (!p || !p->partition_length) continue;
        n=snprintf(out+used,capacity-used,
                   "%s{\"id\":%d,\"name\":\"%s\",\"start\":%lu,\"length\":%lu}",
                   count?",":"",(int)partitions[i].id,partitions[i].name,
                   (unsigned long)p->partition_start_addr,(unsigned long)p->partition_length);
        if (n<0 || (size_t)n>=capacity-used) goto overflow;
        used+=(size_t)n; count++;
    }
    n=snprintf(out+used,capacity-used,"]},\"log\":{\"memory_only\":true,\"capacity_records\":%u,\"count\":%u,\"wrap_count\":%lu,\"dropped_count\":%lu,\"ram_bytes\":%lu}}",
               OPENM1_LOG_RECORD_COUNT,log_status.count,(unsigned long)log_status.wrap_count,
               (unsigned long)log_status.dropped_count,(unsigned long)openm1_log_ram_bytes());
    if (n<0 || (size_t)n>=capacity-used) goto overflow;
    return;
overflow:
    snprintf(out,capacity,"{\"error\":\"system stats buffer too small\"}");
}
