#include "openm1_log.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static openm1_log_record_t records[OPENM1_LOG_RECORD_COUNT];
static char scratch[96];
static mico_mutex_t log_mutex;
static uint32_t next_sequence=1,wrap_count,dropped_count;
static unsigned head,count;
static int logger_available;

const char *openm1_log_level_name(openm1_log_level_t level)
{
    return level==OPENM1_LOG_ERROR?"ERROR":level==OPENM1_LOG_WARN?"WARN":"INFO";
}
size_t openm1_log_ram_bytes(void)
{
    return sizeof(records)+sizeof(scratch)+sizeof(log_mutex)+sizeof(next_sequence)+
           sizeof(wrap_count)+sizeof(dropped_count)+sizeof(head)+sizeof(count)+
           sizeof(logger_available);
}
OSStatus openm1_log_init(void)
{
    OSStatus err=mico_rtos_init_mutex(&log_mutex);
    logger_available=err==kNoErr;
    return err;
}
void openm1_log_write(openm1_log_level_t level,const char *module,const char *fmt,...)
{
    va_list args;
    unsigned slot;
    uint32_t now=mico_rtos_get_time();
    if (!module) module="SYSTEM";
    if (!fmt) return;
    if (!logger_available) {
        printf("[%lu][%s][%s] ",(unsigned long)now,openm1_log_level_name(level),module);
        va_start(args,fmt); vprintf(fmt,args); va_end(args);
        printf("\r\n");
        return;
    }
    mico_rtos_lock_mutex(&log_mutex);
    va_start(args,fmt);
    vsnprintf(scratch,sizeof(scratch),fmt,args);
    va_end(args);
    slot=head;
    records[slot].seq=next_sequence++;
    records[slot].uptime_ms=now;
    records[slot].level=(uint8_t)level;
    snprintf(records[slot].module,sizeof(records[slot].module),"%s",module);
    snprintf(records[slot].message,sizeof(records[slot].message),"%s",scratch);
    if (count<OPENM1_LOG_RECORD_COUNT) count++;
    else dropped_count++;
    head=(head+1u)%OPENM1_LOG_RECORD_COUNT;
    if (!head) wrap_count++;
    printf("[%lu][%s][%s] %s\r\n",(unsigned long)now,
           openm1_log_level_name(level),records[slot].module,records[slot].message);
    mico_rtos_unlock_mutex(&log_mutex);
}
void openm1_log_clear(void)
{
    if (!logger_available) return;
    mico_rtos_lock_mutex(&log_mutex);
    head=count=0;
    mico_rtos_unlock_mutex(&log_mutex);
}
void openm1_log_status(openm1_log_status_t *out)
{
    if (!out) return;
    memset(out,0,sizeof(*out));
    out->next_sequence=next_sequence;
    out->available=logger_available;
    if (!logger_available) return;
    mico_rtos_lock_mutex(&log_mutex);
    out->count=count;
    out->next_sequence=next_sequence;
    out->oldest_sequence=count?records[(head+OPENM1_LOG_RECORD_COUNT-count)%OPENM1_LOG_RECORD_COUNT].seq:0;
    out->newest_sequence=count?records[(head+OPENM1_LOG_RECORD_COUNT-1u)%OPENM1_LOG_RECORD_COUNT].seq:0;
    out->wrap_count=wrap_count;
    out->dropped_count=dropped_count;
    mico_rtos_unlock_mutex(&log_mutex);
}
int openm1_log_get_record(uint32_t seq,openm1_log_record_t *out)
{
    unsigned i,index;
    int found=0;
    if (!logger_available || !out) return 0;
    mico_rtos_lock_mutex(&log_mutex);
    for (i=0;i<count;i++) {
        index=(head+OPENM1_LOG_RECORD_COUNT-count+i)%OPENM1_LOG_RECORD_COUNT;
        if (records[index].seq==seq) { *out=records[index]; found=1; break; }
    }
    mico_rtos_unlock_mutex(&log_mutex);
    return found;
}
static size_t append_json_string(char *out,size_t cap,size_t pos,const char *s)
{
    unsigned char c;
    if (pos+1>=cap) return pos;
    out[pos++]='"';
    while ((c=(unsigned char)*s++)) {
        if (c=='"'||c=='\\') {
            if (pos+2>=cap) break;
            out[pos++]='\\';out[pos++]=(char)c;
        } else if (c<32) {
            if (pos+6>=cap) break;
            pos+=(size_t)snprintf(out+pos,cap-pos,"\\u%04x",(unsigned)c);
        } else {
            if (pos+1>=cap) break;
            out[pos++]=(char)c;
        }
    }
    if (pos+1<cap) { out[pos++]='"';out[pos]=0; }
    return pos;
}
void openm1_log_json(uint32_t after,char *out,size_t capacity)
{
    openm1_log_status_t s;
    openm1_log_record_t r;
    uint32_t seq,start;
    size_t used;
    unsigned emitted=0;
    int reset;
    int n;
    if (!out || capacity<128) return;
    openm1_log_status(&s);
    reset=s.count && after && (after<s.oldest_sequence-1u || after>s.newest_sequence);
    start=reset?s.oldest_sequence:after+1u;
    if (s.count && start<s.oldest_sequence) start=s.oldest_sequence;
    n=snprintf(out,capacity,"{\"available\":%s,\"memory_only\":true,\"capacity\":%u,\"count\":%u,\"oldest_sequence\":%lu,\"newest_sequence\":%lu,\"next_sequence\":%lu,\"wrapped\":%s,\"wrap_count\":%lu,\"dropped_count\":%lu,\"cursor_reset\":%s,\"records\":[",
        s.available?"true":"false",OPENM1_LOG_RECORD_COUNT,s.count,
        (unsigned long)s.oldest_sequence,(unsigned long)s.newest_sequence,
        (unsigned long)s.next_sequence,s.wrap_count?"true":"false",
        (unsigned long)s.wrap_count,(unsigned long)s.dropped_count,reset?"true":"false");
    if (n<0 || (size_t)n>=capacity) { out[0]=0; return; }
    used=(size_t)n;
    if (s.available && s.count) for (seq=start;seq<=s.newest_sequence && emitted<OPENM1_LOG_PAGE_RECORDS;seq++) {
        if (!openm1_log_get_record(seq,&r)) continue;
        n=snprintf(out+used,capacity-used,"%s{\"seq\":%lu,\"uptime_ms\":%lu,\"level\":\"%s\",\"module\":",
                   emitted?",":"",(unsigned long)r.seq,(unsigned long)r.uptime_ms,
                   openm1_log_level_name((openm1_log_level_t)r.level));
        if (capacity-used<180u || n<0 || (size_t)n>=capacity-used-140u) break;
        used+=(size_t)n;
        used=append_json_string(out,capacity,used,r.module);
        if (used+12>=capacity) break;
        memcpy(out+used,",\"message\":",11);used+=11;out[used]=0;
        used=append_json_string(out,capacity,used,r.message);
        if (used+3>=capacity) break;
        out[used++]='}';out[used]=0;
        emitted++;
    }
    if (used+3<capacity) { out[used++]=']';out[used++]='}';out[used]=0; }
}
