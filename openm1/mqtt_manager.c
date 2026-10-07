#include "mqtt_manager.h"
#include "openm1_log.h"
#include "config_store.h"
#include "ha_policy.h"
#include "homeassistant.h"
#include "json_min.h"
#include "m1_sensor.h"
#include "recovery.h"
#include "wifi_manager.h"
#include "system_stats.h"
#include "MQTTClient.h"
#include "mqtt_bounded_read.h"
#include "mqtt_diagnostics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char state[24];
    char error[80];
    char failure_stage[24];
    char start_block_reason[24];
    uint32_t last_connect_ms,last_publish_ms,last_discovery_ms,publish_count;
    uint32_t connect_count,disconnect_count,mqtt_connect_fail_count,publish_fail_count;
    uint32_t yield_fail_count,read_timeout_count,peer_close_count;
    uint32_t last_disconnect_ms,last_connected_duration_ms,connected_since_ms;
    uint32_t worker_retry_not_before_ms;
    int last_error_code,last_socket_read_result,last_socket_error,last_socket_write_result;
    int connected,ha_active;
} mqtt_status_t;
static mqtt_status_t status={{0}};
static mico_mutex_t status_mutex;
static mico_thread_t worker_thread;
static int manager_ready;
static int worker_created,worker_creation_pending;
/* MQTTClient.c reallocates before receiving a body. Reject oversized broker
 * remaining-length fields while they are decoded, before that allocation. */
static openm1_mqtt_read_state_t mqtt_read_state;
static uint32_t mqtt_now_ms(void *context)
{
    (void)context;
    return mico_rtos_get_time();
}
static int mqtt_wait_readable(void *context,int socket,uint32_t timeout_ms)
{
    fd_set readfds;
    struct timeval timeout;
    (void)context;
    FD_ZERO(&readfds);FD_SET(socket,&readfds);
    timeout.tv_sec=timeout_ms/1000u;
    timeout.tv_usec=(timeout_ms%1000u)*1000u;
    return select(socket+1,&readfds,NULL,NULL,&timeout);
}
static int mqtt_recv_bytes(void *context,int socket,unsigned char *buffer,int length)
{
    (void)context;
    return recv(socket,buffer,length,0);
}
static int mqtt_socket_error(void *context,int socket)
{
    int error=0;
    socklen_t size=sizeof(error);
    (void)context;
    return getsockopt(socket,SOL_SOCKET,SO_ERROR,&error,&size)==0?error:-1;
}
static void mqtt_pause_ms(void *context,uint32_t milliseconds)
{
    (void)context;
    mico_thread_msleep(milliseconds);
}
static const openm1_mqtt_read_ops_t mqtt_read_ops={
    mqtt_now_ms,mqtt_wait_readable,mqtt_recv_bytes,mqtt_socket_error,mqtt_pause_ms
};
static void record_failure(mqtt_failure_stage_t stage,int code);
static int openm1_mqtt_read(Network *network,unsigned char *buffer,int length,int timeout_ms)
{
    uint32_t before_timeout=mqtt_read_state.read_timeout_count;
    uint32_t before_close=mqtt_read_state.peer_close_count;
    int result=openm1_mqtt_read_bounded(&mqtt_read_state,&mqtt_read_ops,NULL,
                                       network->my_socket,buffer,length,timeout_ms);
    mico_rtos_lock_mutex(&status_mutex);
    status.read_timeout_count+=mqtt_read_state.read_timeout_count-before_timeout;
    status.peer_close_count+=mqtt_read_state.peer_close_count-before_close;
    status.last_socket_read_result=result;
    if (result<0) status.last_socket_error=mqtt_read_state.last_socket_error;
    mico_rtos_unlock_mutex(&status_mutex);
    return result;
}
static int openm1_mqtt_write(Network *network,unsigned char *buffer,int length,int timeout_ms)
{
    int result=MICO_write(network,buffer,length,timeout_ms);
    int socket_error=result<0?mqtt_socket_error(NULL,network->my_socket):0;
    mico_rtos_lock_mutex(&status_mutex);
    status.last_socket_write_result=result;
    if (result<0) status.last_socket_error=socket_error;
    mico_rtos_unlock_mutex(&status_mutex);
    return result;
}
static void record_failure(mqtt_failure_stage_t stage,int code)
{
    mico_rtos_lock_mutex(&status_mutex);
    snprintf(status.failure_stage,sizeof(status.failure_stage),"%s",mqtt_failure_stage_name(stage));
    status.last_error_code=code;
    if (stage==MQTT_STAGE_MQTT_CONNECT) status.mqtt_connect_fail_count++;
    if (mqtt_failure_is_publish(stage)) status.publish_fail_count++;
    if (mqtt_failure_is_yield(stage)) status.yield_fail_count++;
    mico_rtos_unlock_mutex(&status_mutex);
}
static void record_connected(void)
{
    uint32_t now=mico_rtos_get_time();
    mico_rtos_lock_mutex(&status_mutex);
    status.connect_count++;
    status.last_connect_ms=now;
    status.connected_since_ms=now;
    strcpy(status.failure_stage,"none");
    status.last_error_code=0;
    mico_rtos_unlock_mutex(&status_mutex);
}
static void record_disconnect(void)
{
    uint32_t now=mico_rtos_get_time();
    mico_rtos_lock_mutex(&status_mutex);
    status.disconnect_count++;
    status.last_disconnect_ms=now;
    status.last_connected_duration_ms=now-status.connected_since_ms;
    status.connected_since_ms=0;
    mico_rtos_unlock_mutex(&status_mutex);
}

static void set_state(const char *state,const char *error)
{
    int changed;
    mico_rtos_lock_mutex(&status_mutex);
    changed=strcmp(status.state,state)!=0;
    snprintf(status.state,sizeof(status.state),"%s",state);
    snprintf(status.error,sizeof(status.error),"%s",error?error:"");
    status.connected=!strcmp(state,"connected");
    if (!status.connected) status.ha_active=0;
    mico_rtos_unlock_mutex(&status_mutex);
    if (changed) {
        if (!strcmp(state,"connected")) openm1_log_info("MQTT","connected");
        else if (!strcmp(state,"connecting")) openm1_log_info("MQTT","connecting");
        else if (!strcmp(state,"deferred_low_memory")) openm1_log_warn("MQTT","low-memory deferred");
        else if (!strcmp(state,"reconnecting")) openm1_log_warn("MQTT","disconnected; reconnecting");
        else if (!strcmp(state,"disabled")) openm1_log_info("MQTT","disabled");
    }
}
static void set_start_block(const char *state,const char *reason,const char *error)
{
    mico_rtos_lock_mutex(&status_mutex);
    snprintf(status.start_block_reason,sizeof(status.start_block_reason),"%s",reason);
    mico_rtos_unlock_mutex(&status_mutex);
    set_state(state,error);
}
#define MQTT_WORKER_CREATE_RETRY_MS 30000u
static int valid_topic(const char *value,size_t max)
{
    size_t i,n=strlen(value);
    if (!n || n>=max || value[0]=='/' || value[n-1]=='/') return 0;
    for (i=0;i<n;i++) {
        char c=value[i];
        if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='/'||c=='_'||c=='-')) return 0;
    }
    return strstr(value,"//")==NULL;
}
static int valid_host(const char *value)
{
    size_t i,n=strlen(value);
    if (n>=129) return 0;
    for (i=0;i<n;i++) {
        char c=value[i];
        if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='.'||c=='-')) return 0;
    }
    return 1;
}
static int copy_string(char *out,size_t cap,const json_min_field_t *field)
{
    size_t n;
    if (!field || field->kind!='s') return -1;
    n=strlen(field->value);
    if (n>=cap) return -1;
    memcpy(out,field->value,n+1);
    return 0;
}
static int parse_integer(const json_min_field_t *field,unsigned min,unsigned max,unsigned *out)
{
    unsigned long v;
    char *end;
    if (!field || field->kind!='n') return -1;
    v=strtoul(field->value,&end,10);
    if (*end || v<min || v>max) return -1;
    *out=(unsigned)v;
    return 0;
}
int mqtt_manager_configure(const char *body,size_t length)
{
    /* Called only by the single-client HTTP thread; avoid a ~2 KiB frame. */
    static json_min_field_t fields[12];
    openm1_config_t next;
    int count,i,clear_password=0;
    unsigned number;
    if (!manager_ready || !body || length>1024) return -1;
    count=json_min_parse(body,length,fields,12);
    if (count<0) return -1;
    config_store_get(&next);
    for (i=0;i<count;i++) {
        const json_min_field_t *f=&fields[i];
        if (!strcmp(f->key,"host")) { if (copy_string(next.host,sizeof(next.host),f)) return -1; }
        else if (!strcmp(f->key,"port")) { if (parse_integer(f,1,65535,&number)) return -1; next.port=number; }
        else if (!strcmp(f->key,"username")) { if (copy_string(next.username,sizeof(next.username),f)) return -1; }
        else if (!strcmp(f->key,"password")) { if (copy_string(next.password,sizeof(next.password),f)) return -1; }
        else if (!strcmp(f->key,"clear_password")) { if (f->kind!='b') return -1; clear_password=!strcmp(f->value,"true"); }
        else if (!strcmp(f->key,"client_id")) { if (copy_string(next.client_id,sizeof(next.client_id),f)) return -1; }
        else if (!strcmp(f->key,"base_topic")) { if (copy_string(next.base_topic,sizeof(next.base_topic),f)) return -1; }
        else if (!strcmp(f->key,"publish_interval")) { if (parse_integer(f,2,300,&number)) return -1; next.publish_interval=number; }
        else if (!strcmp(f->key,"discovery_prefix")) { if (copy_string(next.discovery_prefix,sizeof(next.discovery_prefix),f)) return -1; }
        else return -1;
    }
    if (clear_password) next.password[0]=0;
    if (next.password[0] && !next.username[0]) return -1;
    if (!valid_host(next.host) || !valid_topic(next.client_id,sizeof(next.client_id)) ||
        strchr(next.client_id,'/') || !valid_topic(next.base_topic,sizeof(next.base_topic)) ||
        !valid_topic(next.discovery_prefix,sizeof(next.discovery_prefix))) return -1;
    if (next.ha_enabled) {
        /* Topic changes while Discovery is active would strand retained topics. */
        openm1_config_t old;
        config_store_get(&old);
        if (strcmp(old.discovery_prefix,next.discovery_prefix) ||
            strcmp(old.base_topic,next.base_topic)) return -2;
    }
    return config_store_save(&next)==kNoErr?0:-3;
}
int mqtt_manager_start(void)
{
    openm1_config_t config;
    if (!manager_ready) return -3;
    config_store_get(&config);
    if (!config.host[0]) return -2;
    config.mqtt_enabled=1;
    if (config_store_save(&config)!=kNoErr) return -3;
    mqtt_manager_maybe_start(1);
    return 0; /* Enabled configuration starts automatically when prerequisites recover. */
}
int mqtt_manager_stop(void)
{
    openm1_config_t config;
    if (!manager_ready) return -3;
    config_store_get(&config);
    config.mqtt_enabled=0;
    if (config_store_save(&config)!=kNoErr) return -3;
    set_start_block("disabled","disabled","");
    return 0;
}
int mqtt_manager_set_discovery(int enabled)
{
    openm1_config_t config;
    int connected;
    if (!manager_ready) return -3;
    config_store_get(&config);
    mico_rtos_lock_mutex(&status_mutex);
    connected=status.connected;
    mico_rtos_unlock_mutex(&status_mutex);
    if (!ha_policy_can_enable(config.host[0]!=0,config.mqtt_enabled,connected)) return -2;
    config.ha_enabled=enabled?1:0;
    return config_store_save(&config)==kNoErr?0:-3;
}
static int network_open(Network *network,const openm1_config_t *config,mqtt_failure_stage_t *failure_stage)
{
    struct hostent *host;
    struct sockaddr_in address;
    memset(network,0,sizeof(*network));
    network->my_socket=-1;
    openm1_mqtt_read_reset(&mqtt_read_state);
    mico_rtos_lock_mutex(&status_mutex);
    status.last_socket_read_result=0;
    status.last_socket_write_result=0;
    mico_rtos_unlock_mutex(&status_mutex);
    host=gethostbyname(config->host);
    if (!host || !host->h_addr_list || !host->h_addr_list[0]) {
        *failure_stage=MQTT_STAGE_DNS;return -1;
    }
    memset(&address,0,sizeof(address));
    address.sin_family=AF_INET;
    address.sin_port=htons(config->port);
    memcpy(&address.sin_addr,host->h_addr_list[0],sizeof(address.sin_addr));
    network->my_socket=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if (network->my_socket<0) { *failure_stage=MQTT_STAGE_TCP_SOCKET;return -1; }
    if (connect(network->my_socket,(struct sockaddr *)&address,sizeof(address))!=0) {
        *failure_stage=MQTT_STAGE_TCP_CONNECT;
        close(network->my_socket); network->my_socket=-1;return -1;
    }
    network->mqttread=openm1_mqtt_read;
    network->mqttwrite=openm1_mqtt_write;
    network->disconnect=MICO_disconnect;
    return 0;
}
static int publish(Client *client,const char *topic,const char *payload,int retained)
{
    MQTTMessage message;
    memset(&message,0,sizeof(message));
    message.qos=QOS0;message.retained=retained;
    message.payload=(void *)payload;message.payloadlen=strlen(payload);
    return MQTTPublish(client,topic,&message);
}
static int publish_state(Client *client,const openm1_config_t *config)
{
    m1_sensor_snapshot_t sensor;
    char t[24],h[24],p[24],f[24],topic[150],payload[280];
    uint32_t now=mico_rtos_get_time();
    int rc;
    m1_sensor_get_snapshot(&sensor);
    if (sensor.last_update_ms && now-sensor.last_update_ms<30000 && sensor.temperature_valid) snprintf(t,sizeof(t),"%.1f",(double)sensor.temperature); else strcpy(t,"null");
    if (sensor.last_update_ms && now-sensor.last_update_ms<30000 && sensor.humidity_valid) snprintf(h,sizeof(h),"%.1f",(double)sensor.humidity); else strcpy(h,"null");
    if (sensor.last_update_ms && now-sensor.last_update_ms<30000 && sensor.pm25_valid) snprintf(p,sizeof(p),"%u",(unsigned)sensor.pm25); else strcpy(p,"null");
    if (sensor.last_update_ms && now-sensor.last_update_ms<30000 && sensor.formaldehyde_valid) snprintf(f,sizeof(f),"%.3f",(double)sensor.formaldehyde); else strcpy(f,"null");
    snprintf(topic,sizeof(topic),"%s/state",config->base_topic);
    snprintf(payload,sizeof(payload),"{\"temperature\":%s,\"humidity\":%s,\"PM25\":%s,\"formaldehyde\":%s,\"uptime\":%lu,\"rssi\":%d}",
             t,h,p,f,(unsigned long)(now/1000),wifi_manager_station_rssi());
    rc=publish(client,topic,payload,1);
    if (rc==MQTT_SUCCESS) {
        mico_rtos_lock_mutex(&status_mutex);
        status.last_publish_ms=now;status.publish_count++;
        mico_rtos_unlock_mutex(&status_mutex);
        return 0;
    }
    return rc;
}
static int connection_changed(const openm1_config_t *a,const openm1_config_t *b)
{
    return strcmp(a->host,b->host)||a->port!=b->port||strcmp(a->username,b->username)||
           strcmp(a->password,b->password)||strcmp(a->client_id,b->client_id)||
           strcmp(a->base_topic,b->base_topic)||strcmp(a->discovery_prefix,b->discovery_prefix);
}
static void mqtt_worker(mico_thread_arg_t arg)
{
    openm1_config_t config,current;
    Network network;
    Client client;
    MQTTPacket_connectData options=MQTTPacket_connectData_initializer;
    char availability[160];
    unsigned backoff=5;
    uint32_t last_publish=0;
    int client_ready,connected,rc,write_result;
    mqtt_failure_stage_t open_stage=MQTT_STAGE_NONE;
    (void)arg;
    openm1_log_info("MQTT","worker started");
    for (;;) {
        config_store_get(&config);
        if (recovery_ota_busy()) {set_state("disabled","");mico_thread_msleep(500);continue;}
        if (!config.mqtt_enabled || !config.host[0]) {set_state("disabled","");mico_thread_msleep(500);continue;}
        if (!wifi_manager_station_ready()) {set_state("waiting_network","");mico_thread_msleep(1000);continue;}
        set_state("connecting","");
        open_stage=MQTT_STAGE_NONE;
        if (network_open(&network,&config,&open_stage)) {
            record_failure(open_stage,-1);
            openm1_log_error("MQTT","%s failed",mqtt_failure_stage_name(open_stage));
            goto retry;
        }
        memset(&client,0,sizeof(client));client_ready=0;connected=0;
        rc=MQTTClientInit(&client,&network,3000);
        if (rc!=MQTT_SUCCESS) {
            record_failure(MQTT_STAGE_MQTT_CONNECT,rc);
            openm1_log_error("MQTT","client init failed rc=%d",rc);
            goto close_network;
        }
        client_ready=1;
        memset(&options,0,sizeof(options));
        options.MQTTVersion=4;options.keepAliveInterval=30;options.cleansession=1;
        options.clientID.cstring=config.client_id;
        if (config.username[0]) options.username.cstring=config.username;
        if (config.password[0]) options.password.cstring=config.password;
        snprintf(availability,sizeof(availability),"%s/availability",config.base_topic);
        options.willFlag=1;options.will.topicName.cstring=availability;
        options.will.message.cstring="offline";options.will.retained=1;options.will.qos=QOS0;
        rc=MQTTConnect(&client,&options);
        if (rc!=MQTT_SUCCESS) {
            record_failure(MQTT_STAGE_MQTT_CONNECT,rc);
            openm1_log_error("MQTT","CONNECT failed rc=%d",rc);
            goto close_network;
        }
        openm1_log_info("MQTT","CONNACK accepted");
        connected=1;backoff=5;
        record_connected();
        set_state("connected","");
        rc=publish(&client,availability,"online",1);
        if (rc!=MQTT_SUCCESS) {
            record_failure(MQTT_STAGE_AVAILABILITY_PUBLISH,rc);
            openm1_log_error("MQTT","availability publish failed rc=%d",rc);
            goto close_network;
        }
        last_publish=0;
        for (;;) {
            int ha_active;
            config_store_get(&current);
            if (!current.mqtt_enabled || connection_changed(&config,&current)) {
                record_failure(MQTT_STAGE_CONFIG_CHANGED,0);break;
            }
            if (!wifi_manager_station_ready()) {record_failure(MQTT_STAGE_WIFI_LOST,0);break;}
            if (recovery_ota_busy()) break; /* Release socket; reconnect after OTA. */
            mico_rtos_lock_mutex(&status_mutex);ha_active=status.ha_active;mico_rtos_unlock_mutex(&status_mutex);
            if (current.ha_enabled != ha_active) {
                rc=homeassistant_publish(&client,&current,!current.ha_enabled);
                if (rc) {
                    record_failure(MQTT_STAGE_HA_DISCOVERY,rc);
                    openm1_log_error("MQTT","HA discovery publish failed rc=%d",rc);
                    break;
                }
                mico_rtos_lock_mutex(&status_mutex);
                status.ha_active=current.ha_enabled;
                status.last_discovery_ms=mico_rtos_get_time();
                mico_rtos_unlock_mutex(&status_mutex);
            }
            if (!last_publish || mico_rtos_get_time()-last_publish>=current.publish_interval*1000u) {
                rc=publish_state(&client,&current);
                if (rc) {
                    record_failure(MQTT_STAGE_STATE_PUBLISH,rc);
                    openm1_log_error("MQTT","state publish failed rc=%d",rc);
                    break;
                }
                last_publish=mico_rtos_get_time();
            }
            mqtt_read_state.last_result=0;
            mico_rtos_lock_mutex(&status_mutex);
            status.last_socket_write_result=0;
            mico_rtos_unlock_mutex(&status_mutex);
            rc=MQTTYield(&client,200);
            if (rc!=MQTT_SUCCESS) {
                mqtt_failure_stage_t stage;
                mico_rtos_lock_mutex(&status_mutex);
                write_result=status.last_socket_write_result;
                mico_rtos_unlock_mutex(&status_mutex);
                stage=mqtt_yield_failure_stage(mqtt_read_state.last_result,write_result);
                record_failure(stage,rc);
                openm1_log_error("MQTT","MQTTYield failed rc=%d stage=%s",rc,mqtt_failure_stage_name(stage));
                break;
            }
        }
close_network:
        if (connected) {
            record_disconnect();
            if (!recovery_ota_busy()) {
                publish(&client,availability,"offline",1);
                MQTTDisconnect(&client);
            }
        }
        if (client_ready) MQTTClientDeinit(&client);
        MICO_disconnect(&network);
retry:
        config_store_get(&current);
        if (!current.mqtt_enabled) {set_state("disabled","");continue;}
        set_state(wifi_manager_station_ready()?"reconnecting":"waiting_network","连接中断，稍后重试");
        mico_thread_msleep(backoff*1000u);
        if (backoff<30) backoff=backoff*2>30?30:backoff*2;
    }
}
OSStatus mqtt_manager_init(void)
{
    openm1_config_t config;
    OSStatus err=mico_rtos_init_mutex(&status_mutex);
    if (err!=kNoErr) return err;
    config_store_get(&config);
    strcpy(status.state,config.mqtt_enabled?"waiting_worker":"disabled");
    strcpy(status.start_block_reason,config.mqtt_enabled?"waiting_network":"disabled");
    strcpy(status.failure_stage,"none");
    manager_ready=1;
    return kNoErr;
}
void mqtt_manager_maybe_start(int user_requested)
{
    openm1_config_t config;
    OSStatus err;
    int created,pending;
    uint32_t retry_not_before_ms;
    uint32_t next_retry_ms;
    mqtt_start_block_t block;
    (void)user_requested;
    if (!manager_ready) return;
    config_store_get(&config);
    mico_rtos_lock_mutex(&status_mutex);
    created=worker_created;pending=worker_creation_pending;
    retry_not_before_ms=status.worker_retry_not_before_ms;
    mico_rtos_unlock_mutex(&status_mutex);
    if (created || pending) return;
    if (retry_not_before_ms &&
        (int32_t)(mico_rtos_get_time()-retry_not_before_ms)<0) return;
    block=mqtt_start_block_decide(config.mqtt_enabled,config.host[0]!=0,
        wifi_manager_control_running(),wifi_manager_station_ready(),recovery_ota_busy(),
        system_stats_low_memory_safe_mode(),system_stats_stack_fault_quiet_remaining_ms());
    if (block!=MQTT_START_NONE) {
        set_start_block(mqtt_start_state_name(block),mqtt_start_block_name(block),
                        block==MQTT_START_STACK_FAULT_COOLDOWN?"等待栈异常稳定":
                        block==MQTT_START_LOW_MEMORY?"低内存安全模式，MQTT 已延迟":"");
        return;
    }
    mico_rtos_lock_mutex(&status_mutex);
    if (worker_created || worker_creation_pending) {
        mico_rtos_unlock_mutex(&status_mutex); return;
    }
    worker_creation_pending=1;
    mico_rtos_unlock_mutex(&status_mutex);
    if (!system_stats_begin_optional_thread(MQTT_WORKER_STACK)) {
        set_start_block("deferred_low_memory","heap_reserve","内存不足，MQTT 稍后重试");
        mico_rtos_lock_mutex(&status_mutex); worker_creation_pending=0; mico_rtos_unlock_mutex(&status_mutex);
        return;
    }
    printf("BOOT: free heap before MQTT worker = %d\r\n",system_stats_free_heap());
    err=mico_rtos_create_thread(&worker_thread,MICO_APPLICATION_PRIORITY,"openm1_mqtt",mqtt_worker,MQTT_WORKER_STACK,0);
    system_stats_end_thread_creation();
    next_retry_ms=err==kNoErr?0u:mico_rtos_get_time()+MQTT_WORKER_CREATE_RETRY_MS;
    mico_rtos_lock_mutex(&status_mutex);
    worker_created=err==kNoErr;
    worker_creation_pending=0;
    if (err==kNoErr) {
        strcpy(status.start_block_reason,"none");
    }
    status.worker_retry_not_before_ms=next_retry_ms;
    mico_rtos_unlock_mutex(&status_mutex);
    printf("BOOT: free heap after MQTT worker = %d\r\n",system_stats_free_heap());
    if (err!=kNoErr) {
        set_start_block("waiting_worker","worker_create_failed","MQTT worker 创建失败，稍后重试");
        printf("MQTT: worker start failed: %d, free heap = %d\r\n",err,system_stats_free_heap());
    }
}
int mqtt_manager_ready_for_cpu(void)
{
    openm1_config_t config;
    int created;
    if (!manager_ready) return 0;
    config_store_get(&config);
    mico_rtos_lock_mutex(&status_mutex);
    created=worker_created;
    mico_rtos_unlock_mutex(&status_mutex);
    return created || !config.mqtt_enabled || !config.host[0];
}
static void json_escape(char *out,size_t cap,const char *in)
{
    size_t n=0;
    while (*in && n+2<cap) {
        unsigned char c=(unsigned char)*in++;
        if (c=='"'||c=='\\') {out[n++]='\\';out[n++]=c;}
        else if (c>=32) out[n++]=c;
    }
    out[n]=0;
}
void mqtt_manager_status_json(char *out,size_t capacity)
{
    openm1_config_t config;
    mqtt_status_t snapshot;
    char username[130];
    const char *state_name,*block_reason;
    int created,pending,station_ready,low_memory,ota_busy;
    uint32_t quiet_remaining;
    mqtt_start_block_t block;
    config_store_get(&config);
    memset(&snapshot,0,sizeof(snapshot));
    created=0;pending=0;
    if (manager_ready) {
        mico_rtos_lock_mutex(&status_mutex);
        snapshot=status;created=worker_created;pending=worker_creation_pending;
        mico_rtos_unlock_mutex(&status_mutex);
    }
    station_ready=wifi_manager_station_ready();
    low_memory=system_stats_low_memory_safe_mode();
    ota_busy=recovery_ota_busy();
    quiet_remaining=system_stats_stack_fault_quiet_remaining_ms();
    block=mqtt_start_block_decide(config.mqtt_enabled,config.host[0]!=0,
        wifi_manager_control_running(),station_ready,ota_busy,low_memory,quiet_remaining);
    state_name=snapshot.state;
    block_reason=created?"none":mqtt_start_block_name(block);
    if (!created) {
        state_name=mqtt_start_state_name(block);
        if (block==MQTT_START_NONE &&
            (!strcmp(snapshot.start_block_reason,"heap_reserve") ||
             !strcmp(snapshot.start_block_reason,"worker_create_failed")))
            block_reason=snapshot.start_block_reason;
    }
    json_escape(username,sizeof(username),config.username);
    snprintf(out,capacity,"{\"configured\":%s,\"enabled\":%s,\"state\":\"%s\",\"host\":\"%s\",\"port\":%u,"
        "\"username\":\"%s\",\"password_set\":%s,\"client_id\":\"%s\",\"base_topic\":\"%s\","
        "\"worker_created\":%s,\"worker_creation_pending\":%s,\"station_ready\":%s,"
        "\"start_block_reason\":\"%s\",\"stack_fault_quiet_remaining_ms\":%lu,"
        "\"low_memory_safe_mode\":%s,\"ota_busy\":%s,"
        "\"publish_interval\":%u,\"last_connect_ms\":%lu,\"last_publish_ms\":%lu,\"publish_count\":%lu,\"error\":\"%s\","
        "\"failure_stage\":\"%s\",\"last_error_code\":%d,\"connect_count\":%lu,\"disconnect_count\":%lu,"
        "\"mqtt_connect_fail_count\":%lu,\"publish_fail_count\":%lu,\"yield_fail_count\":%lu,"
        "\"read_timeout_count\":%lu,\"peer_close_count\":%lu,\"last_disconnect_ms\":%lu,"
        "\"last_connected_duration_ms\":%lu,\"last_socket_read_result\":%d,\"last_socket_error\":%d,"
        "\"last_socket_write_result\":%d}",
        config.host[0]?"true":"false",config.mqtt_enabled?"true":"false",state_name,
        config.host,config.port,username,config.password[0]?"true":"false",config.client_id,
        config.base_topic,created?"true":"false",pending?"true":"false",
        station_ready?"true":"false",block_reason,(unsigned long)quiet_remaining,
        low_memory?"true":"false",ota_busy?"true":"false",
        config.publish_interval,(unsigned long)snapshot.last_connect_ms,
        (unsigned long)snapshot.last_publish_ms,(unsigned long)snapshot.publish_count,snapshot.error,
        snapshot.failure_stage,snapshot.last_error_code,
        (unsigned long)snapshot.connect_count,(unsigned long)snapshot.disconnect_count,
        (unsigned long)snapshot.mqtt_connect_fail_count,(unsigned long)snapshot.publish_fail_count,
        (unsigned long)snapshot.yield_fail_count,(unsigned long)snapshot.read_timeout_count,
        (unsigned long)snapshot.peer_close_count,(unsigned long)snapshot.last_disconnect_ms,
        (unsigned long)snapshot.last_connected_duration_ms,snapshot.last_socket_read_result,
        snapshot.last_socket_error,snapshot.last_socket_write_result);
}
void homeassistant_status_json(char *out,size_t capacity)
{
    openm1_config_t config;
    mqtt_status_t snapshot;
    config_store_get(&config);
    memset(&snapshot,0,sizeof(snapshot));
    if (manager_ready) {mico_rtos_lock_mutex(&status_mutex);snapshot=status;mico_rtos_unlock_mutex(&status_mutex);}
    snprintf(out,capacity,"{\"enabled\":%s,\"active\":%s,\"discovery_prefix\":\"%s\",\"mqtt_ready\":%s,\"last_publish_ms\":%lu}",
        config.ha_enabled?"true":"false",snapshot.ha_active?"true":"false",config.discovery_prefix,
        ha_policy_can_enable(config.host[0]!=0,config.mqtt_enabled,snapshot.connected)?"true":"false",
        (unsigned long)snapshot.last_discovery_ms);
}
