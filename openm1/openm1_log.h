#pragma once
#include "mico.h"
#include <stddef.h>
#include <stdint.h>

#define OPENM1_LOG_RECORD_COUNT 24u
#define OPENM1_LOG_MESSAGE_MAX 64u
#define OPENM1_LOG_MODULE_MAX 10u
#define OPENM1_LOG_PAGE_RECORDS 8u

typedef enum { OPENM1_LOG_INFO, OPENM1_LOG_WARN, OPENM1_LOG_ERROR } openm1_log_level_t;
typedef struct {
    uint32_t seq,uptime_ms;
    uint8_t level;
    char module[OPENM1_LOG_MODULE_MAX];
    char message[OPENM1_LOG_MESSAGE_MAX];
} openm1_log_record_t;
typedef struct {
    uint32_t next_sequence,oldest_sequence,newest_sequence,wrap_count,dropped_count;
    unsigned count;
    int available;
} openm1_log_status_t;

OSStatus openm1_log_init(void);
void openm1_log_write(openm1_log_level_t level,const char *module,const char *fmt,...);
#define openm1_log_info(module,...) openm1_log_write(OPENM1_LOG_INFO,module,__VA_ARGS__)
#define openm1_log_warn(module,...) openm1_log_write(OPENM1_LOG_WARN,module,__VA_ARGS__)
#define openm1_log_error(module,...) openm1_log_write(OPENM1_LOG_ERROR,module,__VA_ARGS__)
void openm1_log_clear(void);
void openm1_log_status(openm1_log_status_t *out);
int openm1_log_get_record(uint32_t seq,openm1_log_record_t *out);
void openm1_log_json(uint32_t after,char *out,size_t capacity);
size_t openm1_log_ram_bytes(void);
const char *openm1_log_level_name(openm1_log_level_t level);
