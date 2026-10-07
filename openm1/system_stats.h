#pragma once
#include "mico.h"
#include <stddef.h>
#include <stdint.h>

#define SYSTEM_STATS_CPU_STACK 1024u
#define SYSTEM_STATS_CPU_PRIORITY 8u
#define SYSTEM_STATS_CPU_SAMPLE_MS 100u
#define SYSTEM_STATS_CPU_INTERVAL_MS 5000u
#define OPENM1_MIN_HEAP_RESERVE 8192u
#define OPENM1_OTA_HEAP_RESERVE 4096u
#define OPENM1_STACK_FAULT_QUIET_MS 30000u
#define OPENM1_STACK_TASK_NAME_MAX 32u

OSStatus system_stats_init(void);
OSStatus system_stats_register_stack_diagnostic(void);
void system_stats_note_heap(int boot_sample);
int system_stats_free_heap(void);
int system_stats_begin_optional_thread(unsigned stack_bytes);
int system_stats_begin_ota_thread(unsigned stack_bytes);
void system_stats_end_thread_creation(void);
int system_stats_low_memory_safe_mode(void);
void system_stats_enter_safe_mode(void);
void system_stats_set_boot_phase(const char *phase);
void system_stats_maybe_start_cpu(void);
uint32_t system_stats_stack_overflow_count(void);
uint32_t system_stats_stack_fault_quiet_remaining_ms(void);
int system_stats_stack_fault_cpu_ready(void);
void system_stats_last_stack_overflow_task(char *out,size_t capacity);
void system_stats_json(char *out,size_t capacity);
unsigned system_stats_cpu_estimate(uint32_t current,uint32_t baseline);
