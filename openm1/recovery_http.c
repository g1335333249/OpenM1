#include "recovery.h"
#include "wifi_manager.h"
#include "m1_uart.h"
#include "m1_sensor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static mico_thread_t server_thread;
static volatile int http_listening;
int recovery_http_ready(void) { return http_listening; }
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
    const char *reason=code==200?"OK":code==202?"Accepted":code==400?"Bad Request":code==404?"Not Found":
                       code==405?"Method Not Allowed":code==409?"Conflict":code==413?"Payload Too Large":
                       code==503?"Service Unavailable":"Error";
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
static void skip_json_space(const char **p, const char *end)
{
    while (*p<end && (**p==' ' || **p=='\t' || **p=='\r' || **p=='\n')) ++*p;
}
static int read_json_string(const char **p, const char *end, char *out, size_t capacity)
{
    size_t used=0;
    if (*p>=end || *(*p)++!='"') return 0;
    while (*p<end && **p!='"') {
        unsigned char c=(unsigned char)*(*p)++;
        if (c=='\\') {
            if (*p>=end) return 0;
            c=(unsigned char)*(*p)++;
            if (c!='"' && c!='\\' && c!='/') return 0;
        }
        if (c<32 || used+1>=capacity) return 0;
        out[used++]=(char)c;
    }
    if (*p>=end) return 0;
    ++*p;
    out[used]=0;
    return 1;
}
static int parse_wifi_connect(const char *body, size_t length, char ssid[32], char password[64])
{
    const char *p=body,*end=body+length;
    int got_ssid=0,got_password=0;
    char key[16];
    ssid[0]=0; password[0]=0;
    skip_json_space(&p,end);
    if (p>=end || *p++!='{') return 0;
    for (;;) {
        skip_json_space(&p,end);
        if (p<end && *p=='}') { p++; break; }
        if (!read_json_string(&p,end,key,sizeof(key))) return 0;
        skip_json_space(&p,end);
        if (p>=end || *p++!=':') return 0;
        skip_json_space(&p,end);
        if (!strcmp(key,"ssid") && !got_ssid) {
            if (!read_json_string(&p,end,ssid,32)) return 0;
            got_ssid=1;
        } else if (!strcmp(key,"password") && !got_password) {
            if (!read_json_string(&p,end,password,64)) return 0;
            got_password=1;
        } else return 0;
        skip_json_space(&p,end);
        if (p<end && *p==',') { p++; continue; }
        if (p<end && *p=='}') { p++; break; }
        return 0;
    }
    skip_json_space(&p,end);
    return p==end && got_ssid && got_password;
}
static void handle_client(int fd)
{
    char request[2049], json[768], method[8], path[80], url[512];
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
        } else if (!strcmp(path,"/api/wifi/status")) {
            wifi_manager_status_json(json,sizeof(json)); recovery_send_json(fd,200,json);
        } else if (!strcmp(path,"/api/wifi/scan")) {
            recovery_send_json(fd,200,wifi_manager_scan_json());
        } else if (!strcmp(path,"/api/info")) {
            micoMemInfo_t *memory=MicoGetMemoryInfo();
            snprintf(json,sizeof(json),"{\"device\":\"斐讯悟空 M1\",\"firmware\":\"OpenM1 v0.3.1\",\"version\":\"0.3.1\",\"board\":\"MK3080B\",\"kernel\":\"3080B002.023\",\"rf\":\"%s\",\"mode\":\"recovery\",\"mac\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\",\"uptime\":%lu,\"free_heap\":%d}",
                     recovery_rf(),recovery_mac(),recovery_ssid(),RECOVERY_IP,(unsigned long)(mico_rtos_get_time()/1000),memory?memory->free_memory:-1);
            recovery_send_json(fd,200,json);
        } else if (!strcmp(path,"/api/sensors")) {
            m1_sensor_json(json,sizeof(json)); recovery_send_json(fd,200,json);
        } else if (!strcmp(path,"/api/uart/status")) {
            m1_uart_status_json(json,sizeof(json)); recovery_send_json(fd,200,json);
        } else if (!strcmp(path,"/api/uart/raw")) {
            m1_uart_raw_json(json,sizeof(json)); recovery_send_json(fd,200,json);
        } else recovery_send_json(fd,404,"{\"message\":\"Not found\"}");
        goto done;
    }
    if (strcmp(method,"POST")) { recovery_send_json(fd,405,"{\"message\":\"Method not allowed\"}"); goto done; }
    if (!strcmp(path,"/api/reboot")) {
        recovery_send_json(fd,200,"{\"ok\":true,\"message\":\"Rebooting\"}");
        close(fd); mico_thread_msleep(1800); MicoSystemReboot(); return;
    }
    if (!strcmp(path,"/api/wifi/connect")) {
        char body[257],ssid[32],password[64];
        int result;
        if (!seen || !length || length>256 || body_len>length) {
            recovery_send_json(fd,400,"{\"message\":\"Wi-Fi 请求长度无效。\"}"); goto done;
        }
        memcpy(body,request+body_start,body_len);
        while (body_len<length) {
            n=recv(fd,body+body_len,length-body_len,0);
            if (n<=0) { recovery_send_json(fd,400,"{\"message\":\"Wi-Fi 请求中断。\"}"); goto done; }
            body_len+=(size_t)n;
        }
        body[length]=0;
        if (!parse_wifi_connect(body,length,ssid,password)) {
            recovery_send_json(fd,400,"{\"message\":\"Wi-Fi 名称或密码格式无效。\"}"); goto done;
        }
        result=wifi_manager_connect(ssid,password);
        memset(password,0,sizeof(password));
        memset(body,0,sizeof(body));
        if (result==-2) recovery_send_json(fd,409,"{\"message\":\"已有 Wi-Fi 连接正在进行。\"}");
        else if (result==-4) recovery_send_json(fd,409,"{\"message\":\"请先断开当前家庭 Wi-Fi。\"}");
        else if (result==-1) recovery_send_json(fd,400,"{\"message\":\"Wi-Fi 名称或密码长度无效。\"}");
        else if (result!=0) recovery_send_json(fd,503,"{\"message\":\"Wi-Fi 管理器暂不可用。\"}");
        else recovery_send_json(fd,202,"{\"ok\":true,\"message\":\"正在连接家庭 Wi-Fi。\"}");
        goto done;
    }
    if (!strcmp(path,"/api/wifi/disconnect")) {
        if (wifi_manager_disconnect()==0)
            recovery_send_json(fd,200,"{\"ok\":true,\"message\":\"已断开家庭 Wi-Fi，恢复热点仍保持开启。\"}");
        else recovery_send_json(fd,503,"{\"message\":\"断开家庭 Wi-Fi 失败。\"}");
        goto done;
    }
    if (!strcmp(path,"/api/uart/init")) {
        int result;
        if ((seen && length) || body_len) {
            recovery_send_json(fd,400,"{\"ok\":false,\"error\":\"Request body is not allowed\"}"); goto done;
        }
        result=m1_uart_send_init_command();
        if (result==0) recovery_send_json(fd,200,"{\"ok\":true,\"bytes\":12}");
        else if (result==-3) recovery_send_json(fd,409,"{\"ok\":false,\"error\":\"UART init command already pending\"}");
        else if (result==-2) recovery_send_json(fd,503,"{\"ok\":false,\"error\":\"UART1 is not ready\"}");
        else if (result==-5) recovery_send_json(fd,503,"{\"ok\":false,\"error\":\"UART init command timed out\"}");
        else recovery_send_json(fd,503,"{\"ok\":false,\"error\":\"UART init command send failed\"}");
        goto done;
    }
    if (!strcmp(path,"/api/uart/config")) {
        char body[65]; unsigned long baud; int consumed=0,result;
        if (!seen || !length || length>64 || body_len>length) {
            recovery_send_json(fd,400,"{\"message\":\"串口配置请求长度无效。\"}"); goto done;
        }
        memcpy(body,request+body_start,body_len);
        while (body_len<length) {
            n=recv(fd,body+body_len,length-body_len,0);
            if (n<=0) { recovery_send_json(fd,400,"{\"message\":\"串口配置请求中断。\"}"); goto done; }
            body_len+=(size_t)n;
        }
        body[length]=0;
        if (sscanf(body," { \"baud\" : %lu } %n",&baud,&consumed)!=1 ||
            consumed!=(int)length || baud>UINT32_MAX) {
            recovery_send_json(fd,400,"{\"message\":\"波特率格式无效。\"}"); goto done;
        }
        result=m1_uart_set_baud((uint32_t)baud);
        if (result==-1) recovery_send_json(fd,400,"{\"message\":\"不支持该波特率。\"}");
        else if (result) recovery_send_json(fd,503,"{\"message\":\"业务串口尚未就绪。\"}");
        else recovery_send_json(fd,202,"{\"ok\":true,\"message\":\"正在切换业务串口波特率。\"}");
        goto done;
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
                recovery_send_json(fd,400,"{\"message\":\"暂不支持 HTTPS。\"}"); goto done;
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
        http_listening=1;
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
