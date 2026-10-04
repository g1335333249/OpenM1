#include "recovery.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static mico_thread_t server_thread;
int recovery_send_all(int fd, const void *data, size_t len)
{
    const uint8_t *p=data;
    while (len) {
        int sent=send(fd,p,len,0);
        if (sent<=0) return -1;
        p+=sent; len-=sent;
    }
    return 0;
}
void recovery_send_text(int fd, int code, const char *type, const char *body, size_t length)
{
    char header[192];
    const char *reason=code==200?"OK":code==400?"Bad Request":code==404?"Not Found":
                       code==405?"Method Not Allowed":code==409?"Conflict":code==413?"Payload Too Large":"Error";
    int n=snprintf(header,sizeof(header),"HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n",
                   code,reason,type,(unsigned long)length);
    if (n>0 && n<(int)sizeof(header)) {
        if (recovery_send_all(fd,header,n)==0 && length) recovery_send_all(fd,body,length);
    }
}
void recovery_send_json(int fd, int code, const char *json)
{
    recovery_send_text(fd,code,"application/json; charset=utf-8",json,strlen(json));
}
static int parse_content_length(char *headers, uint32_t *value, int *seen)
{
    char *line=strstr(headers,"\r\n");
    *seen=0; *value=0;
    if (!line) return 0;
    line+=2;
    while (*line) {
        char *end=strstr(line,"\r\n");
        char *number,*after; unsigned long parsed;
        if (!end) break;
        *end=0;
        if (!strncasecmp(line,"Content-Length:",15)) {
            if (*seen) return 0;
            number=line+15;
            while (*number==' '||*number=='\t') number++;
            if (!*number || *number=='-') return 0;
            parsed=strtoul(number,&after,10);
            while (*after==' '||*after=='\t') after++;
            if (*after || parsed>UINT32_MAX) return 0;
            *seen=1; *value=(uint32_t)parsed;
        }
        if (!strncasecmp(line,"Transfer-Encoding:",18)) return 0;
        line=end+2;
    }
    return 1;
}
static int extract_url(const char *body, size_t len, char *url, size_t max)
{
    const char *p=body,*end=body+len; size_t n=0;
    while (p<end && (*p==' '||*p=='\r'||*p=='\n'||*p=='\t')) p++;
    if (p>=end || *p++!='{') return 0;
    while (p<end) {
        while (p<end && (*p==' '||*p=='\r'||*p=='\n'||*p=='\t'||*p==',')) p++;
        if (end-p<5 || memcmp(p,"\"url\"",5)) return 0;
        p+=5;
        while (p<end && (*p==' '||*p=='\t')) p++;
        if (p>=end || *p++!=':') return 0;
        while (p<end && (*p==' '||*p=='\t')) p++;
        if (p>=end || *p++!='\"') return 0;
        while (p<end && *p!='\"') {
            if (*p=='\\' || (unsigned char)*p<32 || n+1>=max) return 0;
            url[n++]=*p++;
        }
        if (p>=end || !n) return 0;
        url[n]=0; p++;
        while (p<end && (*p==' '||*p=='\t'||*p=='\r'||*p=='\n')) p++;
        return p<end && *p=='}';
    }
    return 0;
}
static void handle_client(int fd)
{
    char request[2049], json[512], method[8], path[80], url[512];
    size_t used=0,body_len=0; uint32_t length=0;
    int n,seen=0,body_start=-1;
    int timeout_ms=5000;
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout_ms,sizeof(timeout_ms));
    setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout_ms,sizeof(timeout_ms));
    while (used<2048 && body_start<0) {
        n=recv(fd,request+used,2048-used,0);
        if (n<=0) { close(fd); return; }
        used+=(size_t)n; request[used]=0;
        {
            char *boundary=strstr(request,"\r\n\r\n");
            if (boundary) body_start=(int)(boundary+4-request);
        }
    }
    if (body_start<0) { recovery_send_json(fd,400,"{\"message\":\"HTTP header too large\"}"); goto done; }
    if (sscanf(request,"%7s %79s",method,path)!=2) { recovery_send_json(fd,400,"{\"message\":\"Bad request\"}"); goto done; }
    request[body_start-2]=0;
    if (!parse_content_length(request,&length,&seen)) { recovery_send_json(fd,400,"{\"message\":\"Invalid request framing\"}"); goto done; }
    body_len=used-(size_t)body_start;
    if (!strcmp(method,"GET")) {
        if (!strcmp(path,"/")) recovery_send_text(fd,200,"text/html; charset=utf-8",recovery_page,recovery_page_length);
        else if (!strcmp(path,"/api/health")) recovery_send_json(fd,200,"{\"status\":\"ok\",\"recovery\":true}");
        else if (!strcmp(path,"/api/ota/status")) {
            recovery_ota_status_json(json,sizeof(json)); recovery_send_json(fd,200,json);
        } else if (!strcmp(path,"/api/info")) {
            micoMemInfo_t *memory=MicoGetMemoryInfo();
            snprintf(json,sizeof(json),"{\"device\":\"Phicomm M1\",\"firmware\":\"OpenM1 Recovery v0.1.1\",\"version\":\"0.1.1\",\"board\":\"MK3080B\",\"kernel\":\"3080B002.023\",\"rf\":\"%s\",\"mode\":\"Recovery SoftAP\",\"ssid\":\"%s\",\"ip\":\"%s\",\"uptime\":%lu,\"free_heap\":%d}",
                     recovery_rf(),recovery_ssid(),RECOVERY_IP,(unsigned long)(mico_rtos_get_time()/1000),memory?memory->free_memory:-1);
            recovery_send_json(fd,200,json);
        } else recovery_send_json(fd,404,"{\"message\":\"Not found\"}");
        goto done;
    }
    if (strcmp(method,"POST")) { recovery_send_json(fd,405,"{\"message\":\"Method not allowed\"}"); goto done; }
    if (!strcmp(path,"/api/reboot")) {
        recovery_send_json(fd,200,"{\"ok\":true,\"message\":\"Rebooting\"}");
        close(fd); mico_thread_msleep(1800); MicoSystemReboot(); return;
    }
    if (!strcmp(path,"/api/ota/upload") || !strcmp(path,"/api/ota/url")) {
        int is_url=!strcmp(path,"/api/ota/url");
        if (recovery_ota_busy()) { recovery_send_json(fd,409,"{\"message\":\"OTA already in progress\"}"); goto done; }
        if (!seen || !length) { recovery_send_json(fd,400,"{\"message\":\"Content-Length required\"}"); goto done; }
        if (!is_url && (length>RECOVERY_MAX_OTA_SIZE || length<=0x75008u+16u)) {
            recovery_send_json(fd,413,"{\"message\":\"OTA size invalid or too large\"}"); goto done;
        }
        if (is_url) {
            if (length>511) { recovery_send_json(fd,413,"{\"message\":\"URL request too large\"}"); goto done; }
            while (body_len<length) {
                n=recv(fd,request+body_start+body_len,length-body_len,0);
                if (n<=0) { recovery_send_json(fd,400,"{\"message\":\"URL body interrupted\"}"); goto done; }
                body_len+=(size_t)n;
            }
            if (!extract_url(request+body_start,length,url,sizeof(url))) {
                recovery_send_json(fd,400,"{\"message\":\"Invalid URL JSON\"}"); goto done;
            }
            if (!strncmp(url,"https://",8)) {
                recovery_send_json(fd,400,"{\"message\":\"HTTPS is not supported in Recovery v0.1.1\"}"); goto done;
            }
            if (strncmp(url,"http://",7)) { recovery_send_json(fd,400,"{\"message\":\"HTTP URL required\"}"); goto done; }
            if (!recovery_ota_begin(fd,1,0,(uint8_t*)url,strlen(url))) {
                recovery_send_json(fd,409,"{\"message\":\"OTA worker unavailable\"}"); goto done;
            }
        } else {
            if (body_len>length) { recovery_send_json(fd,400,"{\"message\":\"Body exceeds Content-Length\"}"); goto done; }
            if (!recovery_ota_begin(fd,0,length,(uint8_t*)request+body_start,body_len)) {
                recovery_send_json(fd,409,"{\"message\":\"OTA worker unavailable\"}"); goto done;
            }
        }
        return; /* OTA worker owns fd and responds after verification. */
    }
    recovery_send_json(fd,404,"{\"message\":\"Not found\"}");
done:
    close(fd);
}
static void recovery_http_server_thread(mico_thread_arg_t arg)
{
    struct sockaddr_in address;
    int listener,client,on=1;
    (void)arg;
    for (;;) {
        listener=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
        if (listener<0) {
            printf("RECOVERY: HTTP socket failed; retrying\r\n");
            mico_thread_msleep(2000);
            continue;
        }
        setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&on,sizeof(on));
        memset(&address,0,sizeof(address));
        address.sin_len=sizeof(address); address.sin_family=AF_INET;
        address.sin_addr.s_addr=INADDR_ANY; address.sin_port=htons(80);
        if (bind(listener,(struct sockaddr*)&address,sizeof(address))<0 || listen(listener,3)<0) {
            printf("RECOVERY: HTTP bind/listen failed; retrying\r\n");
            close(listener);
            mico_thread_msleep(2000);
            continue;
        }
        printf("RECOVERY: HTTP server listening on 0.0.0.0:80\r\n");
        printf("RECOVERY: OTA service ready\r\n");
        for (;;) {
            client=accept(listener,NULL,NULL);
            if (client>=0) handle_client(client);
            else mico_thread_msleep(100);
        }
    }
}
OSStatus recovery_http_start(void)
{
    printf("RECOVERY: HTTP server starting\r\n");
    return mico_rtos_create_thread(&server_thread,MICO_APPLICATION_PRIORITY,"openm1_http",
             recovery_http_server_thread,RECOVERY_HTTP_STACK,0);
}
