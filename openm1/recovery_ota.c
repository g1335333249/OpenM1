#include "recovery.h"
#include "system_stats.h"
#include "openm1_log.h"
#include "ota_transfer_logic.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define APP_OFFSET 0x75000u
#define APP_START (APP_OFFSET + 8u)
#define MD5_SIZE 16u
#define URL_LIMIT 511u

typedef struct {
    int fd;
    int is_url;
    uint32_t length;
    uint16_t initial_len;
    uint8_t initial[2048];
} ota_job_t;

static ota_job_t job;
static mico_thread_t ota_thread;
static mico_mutex_t status_mutex;
static int mutex_ready;
static char rf_text[64] = "unavailable";
static struct {
    const char *state;
    uint32_t received;
    uint32_t total;
    char message[48];
    int active;
    int prepared,peer_closed;
    uint32_t prepared_at,recv_timeout_count,recv_retry_count,last_progress_ms,erase_duration_ms;
    uint32_t flash_write_fail_count;
    int last_recv_error;
} status = {"idle", 0, 0, "", 0};
static uint8_t io_buffer[RECOVERY_OTA_BUFFER];
static char detected_kernel[16] = "unknown";

void recovery_set_rf(const char *version)
{
    if (version && *version) {
        strncpy(rf_text, version, sizeof(rf_text)-1);
        rf_text[sizeof(rf_text)-1] = 0;
    }
}
const char *recovery_rf(void) { return rf_text; }
static void lock_status(void) { if (mutex_ready) mico_rtos_lock_mutex(&status_mutex); }
static void unlock_status(void) { if (mutex_ready) mico_rtos_unlock_mutex(&status_mutex); }
static void set_status(const char *state, const char *message, uint32_t received, uint32_t total)
{
    lock_status();
    status.state = state;
    status.received = received;
    status.total = total;
    snprintf(status.message, sizeof(status.message), "%s", message);
    unlock_status();
}
static void fail(const char *message)
{
    openm1_log_error("OTA","FAILED: %s",message);
    set_status("failed", message, status.received, status.total);
}
int recovery_ota_busy(void)
{
    int active;
    lock_status();
    if (status.prepared && ota_prepared_expired(mico_rtos_get_time(),status.prepared_at)) {
        status.prepared=0;status.active=0;status.state="idle";
        snprintf(status.message,sizeof(status.message),"Prepared upload expired");
    }
    active = status.active; unlock_status();
    return active;
}
int recovery_ota_prepare(void)
{
    mico_logic_partition_t *part;
    uint32_t started;
    OSStatus err;
    (void)recovery_ota_busy();
    if (!system_stats_begin_ota_thread(RECOVERY_OTA_STACK)) return -2;
    lock_status();
    if (!ota_prepare_can_begin(status.active)) { unlock_status();system_stats_end_thread_creation();return 0; }
    status.active=1;status.prepared=0;status.state="preparing";
    status.received=0;status.total=0;
    snprintf(status.message,sizeof(status.message),"Preparing OTA partition");
    unlock_status();
    system_stats_end_thread_creation();
    openm1_log_info("OTA","prepare begin");
    part=MicoFlashGetInfo(MICO_PARTITION_OTA_TEMP);
    if (!part) { fail("Flash partition unavailable");lock_status();status.active=0;unlock_status();return -1; }
    set_status("erasing","Erasing Flash",0,0);
    openm1_log_info("OTA","erase begin");
    started=mico_rtos_get_time();
    err=MicoFlashErase(MICO_PARTITION_OTA_TEMP,0,part->partition_length);
    lock_status();status.erase_duration_ms=(uint32_t)(mico_rtos_get_time()-started);unlock_status();
    if (err!=kNoErr) { fail("Flash erase failed");lock_status();status.active=0;unlock_status();return -1; }
    openm1_log_info("OTA","erase complete duration=%lu ms",(unsigned long)status.erase_duration_ms);
    lock_status();
    status.prepared=1;status.prepared_at=mico_rtos_get_time();status.state="prepared";
    snprintf(status.message,sizeof(status.message),"Waiting for upload");
    unlock_status();
    return 1;
}
void recovery_ota_status_json(char *out, size_t size)
{
    uint32_t received, total,time_ms,progress_ms,erase_ms,timeouts,retries,write_fails;
    int recv_error,peer_closed;
    char state[20], message[96], kernel[16];
    (void)recovery_ota_busy();
    lock_status();
    snprintf(state, sizeof(state), "%s", status.state);
    snprintf(message, sizeof(message), "%s", status.message);
    snprintf(kernel, sizeof(kernel), "%s", detected_kernel);
    received = status.received; total = status.total;
    progress_ms=status.last_progress_ms;erase_ms=status.erase_duration_ms;
    timeouts=status.recv_timeout_count;retries=status.recv_retry_count;
    write_fails=status.flash_write_fail_count;recv_error=status.last_recv_error;
    peer_closed=status.peer_closed;
    unlock_status();
    time_ms=mico_rtos_get_time();
    snprintf(out, size, "{\"state\":\"%s\",\"progress\":%lu,\"progress_percent\":%lu,\"received\":%lu,\"total\":%lu,\"message\":\"%s\",\"detected_kernel\":\"%s\",\"recv_timeout_count\":%lu,\"recv_retry_count\":%lu,\"last_recv_error\":%d,\"last_progress_ms\":%lu,\"idle_ms\":%lu,\"erase_duration_ms\":%lu,\"flash_write_fail_count\":%lu,\"peer_closed\":%s}",
             state, (unsigned long)(total ? ((uint64_t)received*100u/total) : 0),
             (unsigned long)(total ? ((uint64_t)received*100u/total) : 0),
             (unsigned long)received, (unsigned long)total, message, kernel,
             (unsigned long)timeouts,(unsigned long)retries,recv_error,
             (unsigned long)progress_ms,(unsigned long)(progress_ms?(uint32_t)(time_ms-progress_ms):0u),
             (unsigned long)erase_ms,(unsigned long)write_fails,peer_closed?"true":"false");
}
void recovery_ota_partition_log(void)
{
    mico_logic_partition_t *part = MicoFlashGetInfo(MICO_PARTITION_OTA_TEMP);
    if (part) {
        printf("RECOVERY: OTA partition start = 0x%08lx\r\n", (unsigned long)part->partition_start_addr);
        printf("RECOVERY: OTA partition size = %lu\r\n", (unsigned long)part->partition_length);
    } else printf("RECOVERY: OTA partition unavailable\r\n");
    if (!mutex_ready && mico_rtos_init_mutex(&status_mutex) == kNoErr) mutex_ready = 1;
}
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1]<<8)); }
static uint16_t app_crc_update(uint16_t crc, const uint8_t *data, size_t size)
{
    size_t i;
    for (i=0;i<size;i++) {
        uint16_t c=data[i]; int bit;
        for (bit=0;bit<8;bit++) {
            int treat=c&0x80;
            int bcrc=(crc>>8)&0x80;
            c<<=1; crc=(uint16_t)(crc<<1);
            if (treat!=bcrc) crc^=0x1021;
        }
    }
    return crc;
}
static int flash_read(uint32_t offset, uint8_t *data, uint32_t size)
{
    volatile uint32_t at=offset;
    return MicoFlashRead(MICO_PARTITION_OTA_TEMP, &at, data, size)==kNoErr;
}
static int ota_receive(int socket_fd, uint32_t total, const uint8_t *initial, size_t initial_len, const char *phase)
{
    mico_logic_partition_t *part=MicoFlashGetInfo(MICO_PARTITION_OTA_TEMP);
    volatile uint32_t offset=0;
    uint32_t received=0;
    int count,socket_error,uploading=!strcmp(phase,"uploading");
    uint32_t last_progress=mico_rtos_get_time();
    if (!part || total>part->partition_length || total>RECOVERY_MAX_OTA_SIZE || total<=APP_START+MD5_SIZE) {
        fail("OTA size exceeds partition or is too small"); return 0;
    }
    openm1_log_info("OTA","%s begin total=%lu",uploading?"upload":"download",(unsigned long)total);
    if (!uploading) {
        uint32_t started=mico_rtos_get_time();
        set_status("erasing", "Erasing Flash", 0, total);
        openm1_log_info("OTA","erase begin");
        if (MicoFlashErase(MICO_PARTITION_OTA_TEMP, 0, part->partition_length)!=kNoErr) {
            fail("Flash erase failed"); return 0;
        }
        lock_status();status.erase_duration_ms=(uint32_t)(mico_rtos_get_time()-started);unlock_status();
        openm1_log_info("OTA","erase complete duration=%lu ms",(unsigned long)status.erase_duration_ms);
    }
    last_progress=mico_rtos_get_time();
    set_status(phase, !strcmp(phase,"uploading")?"Uploading":"Downloading", 0, total);
    if (initial_len) {
        if (initial_len>total) { fail("Initial body exceeds OTA length"); return 0; }
        if (MicoFlashWrite(MICO_PARTITION_OTA_TEMP,&offset,(uint8_t*)initial,initial_len)!=kNoErr) {
            lock_status();status.flash_write_fail_count++;unlock_status();
            fail("Flash write failed"); return 0;
        }
        received=(uint32_t)initial_len;
        last_progress=mico_rtos_get_time();
    }
    lock_status();status.last_progress_ms=last_progress;unlock_status();
    while (received<total) {
        ota_recv_result_t action;
        fd_set readfds;
        struct timeval wait;
        int selected;
        uint32_t want=total-received;
        if (want>sizeof(io_buffer)) want=sizeof(io_buffer);
        FD_ZERO(&readfds);FD_SET(socket_fd,&readfds);
        wait.tv_sec=OTA_RECV_TIMEOUT_MS/1000u;
        wait.tv_usec=(OTA_RECV_TIMEOUT_MS%1000u)*1000u;
        errno=0;
        selected=select(socket_fd+1,&readfds,NULL,NULL,&wait);
        if (selected>0) {
            errno=0;
            count=recv(socket_fd, io_buffer, want, 0);
            socket_error=count<0?errno:0;
        } else {
            count=-1;
            socket_error=selected==0?EAGAIN:errno;
        }
        action=ota_recv_step(count,socket_error,mico_rtos_get_time(),last_progress,received,total);
        if (action==OTA_RECV_RETRY) {
            uint32_t retry_count;
            lock_status();status.recv_retry_count++;status.last_recv_error=socket_error;
            if (socket_error!=EINTR) status.recv_timeout_count++;
            retry_count=status.recv_retry_count;
            unlock_status();
            if (retry_count==1 || retry_count%6u==0)
                openm1_log_warn("OTA","recv waiting count=%lu idle=%lu ms",
                    (unsigned long)retry_count,
                    (unsigned long)(uint32_t)(mico_rtos_get_time()-last_progress));
            set_status(phase,"Network waiting; retrying",received,total);
            if (socket_error==EINTR || !socket_error) mico_thread_msleep(10);
            continue;
        }
        if (action==OTA_RECV_STALLED) {
            fail(uploading?"Upload stalled: no data for 60 seconds":"Download stalled: no data for 60 seconds");
            return 0;
        }
        if (action==OTA_RECV_PEER_CLOSED) {
            lock_status();status.peer_closed=1;unlock_status();
            openm1_log_error("OTA","peer closed received=%lu total=%lu",(unsigned long)received,(unsigned long)total);
            fail(uploading?"Upload peer closed":"Download peer closed");return 0;
        }
        if (action==OTA_RECV_SOCKET_ERROR) {
            lock_status();status.last_recv_error=socket_error;unlock_status();
            openm1_log_error("OTA","socket recv error=%d",socket_error);
            fail(uploading?"Upload socket error":"Download socket error");return 0;
        }
        if (MicoFlashWrite(MICO_PARTITION_OTA_TEMP,&offset,io_buffer,count)!=kNoErr) {
            lock_status();status.flash_write_fail_count++;unlock_status();
            fail("Flash write failed"); return 0;
        }
        received+=(uint32_t)count;
        last_progress=mico_rtos_get_time();
        lock_status();status.last_progress_ms=last_progress;unlock_status();
        set_status(phase,!strcmp(phase,"uploading")?"Uploading":"Downloading",received,total);
        if ((received&0x7fff)<(uint32_t)count || received==total)
            openm1_log_info("OTA","received %lu / %lu",(unsigned long)received,(unsigned long)total);
    }
    openm1_log_info("OTA","transfer complete");
    return 1;
}

/* Re-read every byte from OTA_TEMP; the network receive buffer is never trusted for activation. */
int recovery_ota_verify_flash(uint32_t total, uint16_t *boot_crc)
{
    uint8_t header[8], embedded_md5[16], calculated_md5[16];
    uint32_t payload_len, at, left, count;
    uint16_t app_expected, app_crc=0, boot_result;
    uint8_t version_window[12]; size_t version_used=0, i;
    md5_context md5;
    CRC16_Context crc;
    if (total<=APP_START+MD5_SIZE || total>RECOVERY_MAX_OTA_SIZE) { fail("Invalid OTA length"); return 0; }
    if (!flash_read(APP_OFFSET,header,8)) { fail("APP header read failed"); return 0; }
    payload_len=le32(header);
    app_expected=le16(header+4);
    if (app_expected!=le16(header+6)) { fail("APP CRC copies differ"); return 0; }
    if (payload_len==0 || payload_len>total-APP_START-MD5_SIZE || APP_START+payload_len!=total-MD5_SIZE) {
        fail("APP length does not match OTA length"); return 0;
    }
    set_status("verifying","Verifying APP CRC",total,total);
    printf("OTA: verifying app CRC\r\n");
    at=APP_START; left=payload_len;
    while (left) {
        count=left>sizeof(io_buffer)?sizeof(io_buffer):left;
        if (!flash_read(at,io_buffer,count)) { fail("APP read failed"); return 0; }
        app_crc=app_crc_update(app_crc,io_buffer,count);
        at+=count; left-=count;
    }
    if (app_crc!=app_expected) { fail("APP CRC mismatch"); return 0; }
    openm1_log_info("OTA","APP CRC OK; verifying MD5");
    set_status("verifying","Verifying MD5",total,total);
    if (!flash_read(total-MD5_SIZE,embedded_md5,MD5_SIZE)) { fail("MD5 read failed"); return 0; }
    InitMd5(&md5); CRC16_Init(&crc);
    at=0; left=total-MD5_SIZE;
    while (left) {
        count=left>sizeof(io_buffer)?sizeof(io_buffer):left;
        if (!flash_read(at,io_buffer,count)) { fail("OTA read failed"); return 0; }
        Md5Update(&md5,io_buffer,count);
        CRC16_Update(&crc,io_buffer,count);
        if (at<APP_OFFSET) for (i=0;i<count && at+i<APP_OFFSET;i++) {
            if (version_used<sizeof(version_window)) version_window[version_used++]=io_buffer[i];
            else { memmove(version_window,version_window+1,sizeof(version_window)-1); version_window[11]=io_buffer[i]; }
            if (version_used==sizeof(version_window) && !memcmp(version_window,"3080B002.",9) &&
                version_window[9]>='0' && version_window[9]<='9' &&
                version_window[10]>='0' && version_window[10]<='9' &&
                version_window[11]>='0' && version_window[11]<='9') {
                memcpy(detected_kernel,version_window,12); detected_kernel[12]=0;
            }
        }
        at+=count; left-=count;
    }
    Md5Final(&md5,calculated_md5); CRC16_Final(&crc,&boot_result);
    if (memcmp(embedded_md5,calculated_md5,MD5_SIZE)) { fail("OTA MD5 mismatch"); return 0; }
    *boot_crc=boot_result;
    printf("OTA: detected kernel = %s\r\n",detected_kernel);
    openm1_log_info("OTA","MD5 OK; OTA CRC16=%04x",boot_result);
    return 1;
}
static int parse_url(const char *url, char *host, size_t host_size, uint16_t *port, const char **path)
{
    const char *authority,*slash,*colon; size_t len;
    if (!strncmp(url,"https://",8)) return -2;
    if (strncmp(url,"http://",7)) return -1;
    authority=url+7; slash=strchr(authority,'/'); if (!slash) slash=url+strlen(url);
    colon=memchr(authority,':',slash-authority);
    len=(size_t)((colon?colon:slash)-authority);
    if (!len || len>=host_size) return -1;
    memcpy(host,authority,len); host[len]=0;
    *port=80;
    if (colon) {
        char *end; unsigned long n=strtoul(colon+1,&end,10);
        if (end!=slash || !n || n>65535) return -1;
        *port=(uint16_t)n;
    }
    *path=*slash?slash:"/";
    return 0;
}
static int url_download(const char *url)
{
    char host[128], request[768], header[2048];
    const char *path; uint16_t port; struct hostent *resolved;
    struct sockaddr_in address; int remote=-1,n,header_len=0,body_at=-1,code=0;
    unsigned long length=0; char *line,*next;
    int parsed=parse_url(url,host,sizeof(host),&port,&path);
    if (parsed==-2) { fail("此版本暂不支持 HTTPS"); return 0; }
    if (parsed) { fail("Invalid HTTP URL"); return 0; }
    resolved=gethostbyname(host);
    if (!resolved || !resolved->h_addr) { fail("DNS lookup failed"); return 0; }
    memset(&address,0,sizeof(address)); address.sin_len=sizeof(address); address.sin_family=AF_INET;
    address.sin_port=htons(port); memcpy(&address.sin_addr,resolved->h_addr,4);
    remote=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if (remote>=0) { int timeout_ms=15000; setsockopt(remote,SOL_SOCKET,SO_RCVTIMEO,&timeout_ms,sizeof(timeout_ms)); setsockopt(remote,SOL_SOCKET,SO_SNDTIMEO,&timeout_ms,sizeof(timeout_ms)); }
    if (remote<0 || connect(remote,(struct sockaddr*)&address,sizeof(address))<0) { fail("HTTP connection failed"); goto end; }
    n=snprintf(request,sizeof(request),"GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",path,host);
    if (n<0 || n>=(int)sizeof(request) || recovery_send_all(remote,request,n)<0) { fail("HTTP request failed"); goto end; }
    while (header_len<(int)sizeof(header)-1 && body_at<0) {
        n=recv(remote,header+header_len,sizeof(header)-1-header_len,0);
        if (n<=0) { fail("HTTP response interrupted"); goto end; }
        header_len+=n; header[header_len]=0;
        line=strstr(header,"\r\n\r\n"); if (line) body_at=(int)(line+4-header);
    }
    if (body_at<0) { fail("HTTP headers too large"); goto end; }
    if (sscanf(header,"HTTP/%*u.%*u %d",&code)!=1 || code!=200) {
        fail(code==301||code==302?"HTTP redirect unsupported; use direct URL":"HTTP server did not return 200"); goto end;
    }
    header[body_at-2]=0;
    line=strstr(header,"\r\n"); if (!line) { fail("Malformed HTTP response"); goto end; }
    line+=2;
    while (*line) {
        next=strstr(line,"\r\n"); if (!next) break;
        *next=0;
        if (!strncasecmp(line,"Content-Length:",15)) {
            char *end; length=strtoul(line+15,&end,10);
            while (*end==' '||*end=='\t') end++;
            if (*end || !length) { fail("Invalid Content-Length"); goto end; }
        }
        if (!strncasecmp(line,"Transfer-Encoding:",18) && strstr(line+18,"chunked")) { fail("Chunked transfer unsupported"); goto end; }
        line=next+2;
    }
    if (!length) { fail("Content-Length required"); goto end; }
    if (length>UINT32_MAX || !ota_receive(remote,(uint32_t)length,(uint8_t*)header+body_at,header_len-body_at,"downloading")) goto end;
    close(remote); return 1;
end:
    if (remote>=0) close(remote);
    return 0;
}
static void recovery_ota_upload_handler(mico_thread_arg_t arg)
{
    uint32_t total=job.length; uint16_t boot_crc=0; int ok=0;
    (void)arg;
    if (!job.is_url) { int timeout_ms=OTA_RECV_TIMEOUT_MS; setsockopt(job.fd,SOL_SOCKET,SO_RCVTIMEO,&timeout_ms,sizeof(timeout_ms)); }
    if (job.is_url) {
        char url[URL_LIMIT+1];
        memcpy(url,job.initial,job.initial_len); url[job.initial_len]=0;
        openm1_log_info("OTA","URL begin");
        ok=url_download(url);
        total=status.total;
    } else {
        openm1_log_info("OTA","upload begin");
        ok=ota_receive(job.fd,total,job.initial,job.initial_len,"uploading");
    }
    if (ok) ok=recovery_ota_verify_flash(total,&boot_crc);
    if (ok) {
        OSStatus result;
        set_status("ready","OTA verified",total,total);
        mico_Context_t *context=mico_system_context_get();
        set_status("activating","Writing upgrade flag",total,total);
        printf("OTA: activating\r\n");
        result=mico_ota_switch_to_new_fw((int)(total-MD5_SIZE),boot_crc);
        /* The SDK switch function discards update errors. Check a second persisted update. */
        if (result==kNoErr) result=mico_system_context_update(context);
        if (result!=kNoErr) fail("Boot table update failed");
        else {
            openm1_log_info("OTA","boot table updated");
            set_status("rebooting","Upgrade verified; rebooting",total,total);
            recovery_send_json(job.fd,200,"{\"ok\":true,\"message\":\"OTA verified. Device will reboot.\"}");
            close(job.fd);
            job.fd=-1;
            mico_thread_msleep(2000);
            openm1_log_info("OTA","reboot requested");
            MicoSystemReboot();
        }
    }
    if (job.fd>=0 && (!ok || !strcmp(status.state,"failed")))
        recovery_send_json(job.fd,400,"{\"ok\":false,\"message\":\"OTA failed. See /api/ota/status for details.\"}");
    if (job.fd>=0) close(job.fd);
    lock_status(); status.active=0; unlock_status();
    mico_rtos_delete_thread(NULL);
}
static void recovery_ota_url_handler(mico_thread_arg_t arg) { recovery_ota_upload_handler(arg); }
int recovery_ota_begin(int fd, int is_url, uint32_t length, const uint8_t *initial, size_t initial_len)
{
    OSStatus result;
    if (initial_len>sizeof(job.initial) || (is_url && initial_len>URL_LIMIT)) return 0;
    (void)recovery_ota_busy(); /* Expires an abandoned prepared slot. */
    if (!system_stats_begin_ota_thread(RECOVERY_OTA_STACK))
        return -2;
    lock_status();
    if (!ota_upload_can_begin(status.active,status.prepared,is_url) ||
        (!is_url && strcmp(status.state,"prepared"))) {
        int missing_prepare=!is_url && !status.active;
        unlock_status(); system_stats_end_thread_creation(); return missing_prepare?-3:0;
    }
    status.prepared=0;
    status.active=1; status.state=is_url?"downloading":"uploading";
    status.received=0; status.total=is_url?0:length;
    status.recv_timeout_count=0;status.recv_retry_count=0;status.last_recv_error=0;
    status.last_progress_ms=0;status.flash_write_fail_count=0;status.peer_closed=0;
    status.message[0]=0;
    strcpy(detected_kernel,"unknown");
    unlock_status();
    job.fd=fd; job.is_url=is_url; job.length=length;
    job.initial_len=(uint16_t)initial_len;
    if (initial_len) memcpy(job.initial,initial,initial_len);
    result=mico_rtos_create_thread(&ota_thread,MICO_APPLICATION_PRIORITY,"openm1_ota",
            is_url?recovery_ota_url_handler:recovery_ota_upload_handler,RECOVERY_OTA_STACK,0);
    system_stats_end_thread_creation();
    if (result!=kNoErr) { fail("OTA worker start failed"); lock_status(); status.active=0; unlock_status(); return 0; }
    return 1;
}
