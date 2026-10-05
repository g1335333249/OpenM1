#include "system_stats.h"
#include "mico_hal/mico_flash.h"
#include <stdio.h>
#include <string.h>

static mico_mutex_t stats_mutex;
static mico_thread_t cpu_thread;
static int stats_ready,cpu_sample_ready;
static unsigned cpu_usage_percent;
static uint32_t max_idle_iterations;

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
    stats_ready=1;
    err=mico_rtos_create_thread(&cpu_thread,SYSTEM_STATS_CPU_PRIORITY,
                               "openm1_cpu_sample",cpu_sampler,SYSTEM_STATS_CPU_STACK,0);
    if (err!=kNoErr) printf("STATS: CPU sampler unavailable: %d\r\n",err);
    return err;
}

void system_stats_json(char *out,size_t capacity)
{
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
    size_t i,used=0;
    int n,count=0;
    if (!out || !capacity) return;
    if (stats_ready) {
        mico_rtos_lock_mutex(&stats_mutex);
        usage=cpu_usage_percent; ready=cpu_sample_ready;
        mico_rtos_unlock_mutex(&stats_mutex);
    }
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
       "\"free_chunks\":%d,\"used_percent\":%u},"
       "\"flash\":{\"filesystem\":false,\"type\":\"raw_partitions\",\"partitions\":[",
       cpu_value,ready?"true":"false",total,allocated,free_bytes,chunks,used_percent);
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
    n=snprintf(out+used,capacity-used,"]}}");
    if (n<0 || (size_t)n>=capacity-used) goto overflow;
    return;
overflow:
    snprintf(out,capacity,"{\"error\":\"system stats buffer too small\"}");
}
