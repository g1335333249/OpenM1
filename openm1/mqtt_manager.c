#include "mqtt_manager.h"
#include "config_store.h"
#include "ha_policy.h"
#include "homeassistant.h"
#include "json_min.h"
#include "m1_sensor.h"
#include "recovery.h"
#include "wifi_manager.h"
#include "MQTTClient.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char state[20];
    char error[80];
    uint32_t last_connect_ms,last_publish_ms,last_discovery_ms,publish_count;
    int connected,ha_active;
} mqtt_status_t;
static mqtt_status_t status={{0}};
static mico_mutex_t status_mutex;
static mico_thread_t worker_thread;
static int manager_ready;
/* MQTTClient.c reallocates before receiving a body. Reject oversized broker
 * remaining-length fields while they are decoded, before that allocation. */
static unsigned rx_phase,rx_remaining,rx_multiplier;
static int bounded_mqtt_read(Network *network,unsigned char *buffer,int length,int timeout_ms)
{
    int got=MICO_read(network,buffer,length,timeout_ms);
    if (got!=length || got<=0) return got;
    if (rx_phase==0 && length==1) {
        rx_phase=1;rx_remaining=0;rx_multiplier=1;
    } else if (rx_phase==1 && length==1) {
        rx_remaining+=(buffer[0]&127u)*rx_multiplier;
        if (rx_remaining>1024u) return -1;
        if (buffer[0]&128u) rx_multiplier*=128u;
        else rx_phase=rx_remaining?2u:0u;
    } else if (rx_phase==2) rx_phase=0;
    return got;
}

static void set_state(const char *state,const char *error)
{
    mico_rtos_lock_mutex(&status_mutex);
    snprintf(status.state,sizeof(status.state),"%s",state);
    snprintf(status.error,sizeof(status.error),"%s",error?error:"");
    status.connected=!strcmp(state,"connected");
    if (!status.connected) status.ha_active=0;
    mico_rtos_unlock_mutex(&status_mutex);
}
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
    return config_store_save(&config)==kNoErr?0:-3;
}
int mqtt_manager_stop(void)
{
    openm1_config_t config;
    if (!manager_ready) return -3;
    config_store_get(&config);
    config.mqtt_enabled=0;
    return config_store_save(&config)==kNoErr?0:-3;
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
static int network_open(Network *network,const openm1_config_t *config)
{
    struct hostent *host;
    struct sockaddr_in address;
    int timeout=500;
    memset(network,0,sizeof(*network));
    network->my_socket=-1;
    rx_phase=0;rx_remaining=0;rx_multiplier=1;
    host=gethostbyname(config->host);
    if (!host || !host->h_addr_list || !host->h_addr_list[0]) return -1;
    memset(&address,0,sizeof(address));
    address.sin_family=AF_INET;
    address.sin_port=htons(config->port);
    memcpy(&address.sin_addr,host->h_addr_list[0],sizeof(address.sin_addr));
    network->my_socket=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if (network->my_socket<0) return -1;
    setsockopt(network->my_socket,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    setsockopt(network->my_socket,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    if (connect(network->my_socket,(struct sockaddr *)&address,sizeof(address))!=0) {
        close(network->my_socket); network->my_socket=-1;return -1;
    }
    network->mqttread=bounded_mqtt_read;
    network->mqttwrite=MICO_write;
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
    m1_sensor_get_snapshot(&sensor);
    if (sensor.last_update_ms && now-sensor.last_update_ms<30000 && sensor.temperature_valid) snprintf(t,sizeof(t),"%.1f",(double)sensor.temperature); else strcpy(t,"null");
    if (sensor.last_update_ms && now-sensor.last_update_ms<30000 && sensor.humidity_valid) snprintf(h,sizeof(h),"%.1f",(double)sensor.humidity); else strcpy(h,"null");
    if (sensor.last_update_ms && now-sensor.last_update_ms<30000 && sensor.pm25_valid) snprintf(p,sizeof(p),"%u",(unsigned)sensor.pm25); else strcpy(p,"null");
    if (sensor.last_update_ms && now-sensor.last_update_ms<30000 && sensor.formaldehyde_valid) snprintf(f,sizeof(f),"%.3f",(double)sensor.formaldehyde); else strcpy(f,"null");
    snprintf(topic,sizeof(topic),"%s/state",config->base_topic);
    snprintf(payload,sizeof(payload),"{\"temperature\":%s,\"humidity\":%s,\"PM25\":%s,\"formaldehyde\":%s,\"uptime\":%lu,\"rssi\":%d}",
             t,h,p,f,(unsigned long)(now/1000),wifi_manager_station_rssi());
    if (publish(client,topic,payload,1)==MQTT_SUCCESS) {
        mico_rtos_lock_mutex(&status_mutex);
        status.last_publish_ms=now;status.publish_count++;
        mico_rtos_unlock_mutex(&status_mutex);
        return 0;
    }
    return -1;
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
    int client_ready,connected;
    (void)arg;
    for (;;) {
        config_store_get(&config);
        if (recovery_ota_busy()) {set_state("disabled","");mico_thread_msleep(500);continue;}
        if (!config.mqtt_enabled || !config.host[0]) {set_state("disabled","");mico_thread_msleep(500);continue;}
        if (!wifi_manager_station_ready()) {set_state("waiting_network","");mico_thread_msleep(1000);continue;}
        set_state("connecting","");
        if (network_open(&network,&config)) goto retry;
        memset(&client,0,sizeof(client));client_ready=0;connected=0;
        if (MQTTClientInit(&client,&network,3000)!=MQTT_SUCCESS) goto close_network;
        client_ready=1;
        memset(&options,0,sizeof(options));
        options.MQTTVersion=4;options.keepAliveInterval=30;options.cleansession=1;
        options.clientID.cstring=config.client_id;
        if (config.username[0]) options.username.cstring=config.username;
        if (config.password[0]) options.password.cstring=config.password;
        snprintf(availability,sizeof(availability),"%s/availability",config.base_topic);
        options.willFlag=1;options.will.topicName.cstring=availability;
        options.will.message.cstring="offline";options.will.retained=1;options.will.qos=QOS0;
        if (MQTTConnect(&client,&options)!=MQTT_SUCCESS) goto close_network;
        connected=1;backoff=5;
        set_state("connected","");
        mico_rtos_lock_mutex(&status_mutex);status.last_connect_ms=mico_rtos_get_time();mico_rtos_unlock_mutex(&status_mutex);
        if (publish(&client,availability,"online",1)!=MQTT_SUCCESS) goto close_network;
        last_publish=0;
        for (;;) {
            int ha_active;
            config_store_get(&current);
            if (!current.mqtt_enabled || !wifi_manager_station_ready() || connection_changed(&config,&current)) break;
            if (recovery_ota_busy()) {mico_thread_msleep(500);continue;}
            mico_rtos_lock_mutex(&status_mutex);ha_active=status.ha_active;mico_rtos_unlock_mutex(&status_mutex);
            if (current.ha_enabled != ha_active) {
                if (homeassistant_publish(&client,&current,!current.ha_enabled)) break;
                mico_rtos_lock_mutex(&status_mutex);
                status.ha_active=current.ha_enabled;
                status.last_discovery_ms=mico_rtos_get_time();
                mico_rtos_unlock_mutex(&status_mutex);
            }
            if (!last_publish || mico_rtos_get_time()-last_publish>=current.publish_interval*1000u) {
                if (publish_state(&client,&current)) break;
                last_publish=mico_rtos_get_time();
            }
            if (MQTTYield(&client,200)!=MQTT_SUCCESS) break;
        }
        if (connected) publish(&client,availability,"offline",1);
        if (connected) MQTTDisconnect(&client);
close_network:
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
    OSStatus err=mico_rtos_init_mutex(&status_mutex);
    if (err!=kNoErr) return err;
    strcpy(status.state,"disabled");manager_ready=1;
    err=mico_rtos_create_thread(&worker_thread,MICO_APPLICATION_PRIORITY,"openm1_mqtt",mqtt_worker,MQTT_WORKER_STACK,0);
    if (err!=kNoErr) manager_ready=0;
    return err;
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
    config_store_get(&config);
    memset(&snapshot,0,sizeof(snapshot));
    if (manager_ready) {mico_rtos_lock_mutex(&status_mutex);snapshot=status;mico_rtos_unlock_mutex(&status_mutex);}
    json_escape(username,sizeof(username),config.username);
    snprintf(out,capacity,"{\"configured\":%s,\"enabled\":%s,\"state\":\"%s\",\"host\":\"%s\",\"port\":%u,"
        "\"username\":\"%s\",\"password_set\":%s,\"client_id\":\"%s\",\"base_topic\":\"%s\","
        "\"publish_interval\":%u,\"last_connect_ms\":%lu,\"last_publish_ms\":%lu,\"publish_count\":%lu,\"error\":\"%s\"}",
        config.host[0]?"true":"false",config.mqtt_enabled?"true":"false",snapshot.state,
        config.host,config.port,username,config.password[0]?"true":"false",config.client_id,
        config.base_topic,config.publish_interval,(unsigned long)snapshot.last_connect_ms,
        (unsigned long)snapshot.last_publish_ms,(unsigned long)snapshot.publish_count,snapshot.error);
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
