#include "network_health.h"
#include "wifi_manager.h"
#include "m1_display.h"
#include "recovery.h"
#include "mico_socket.h"
#include <stdio.h>
#include <string.h>

static const char *const probe_hosts[2]={"223.5.5.5","1.1.1.1"};
static mico_mutex_t health_mutex;
static mico_thread_t health_thread;
static network_health_snapshot_t health;
static int health_ready;
static volatile int check_now;

static const char *state_name(network_health_state_t state)
{
    switch (state) {
    case NETWORK_CHECKING: return "checking";
    case NETWORK_ONLINE: return "online";
    case NETWORK_NO_INTERNET: return "no_internet";
    default: return "no_wifi";
    }
}

static void apply_state(network_health_state_t state)
{
    m1_net_display_state_t target=state==NETWORK_ONLINE?M1_NET_DISPLAY_ONLINE:
        state==NETWORK_NO_INTERNET?M1_NET_DISPLAY_NO_INTERNET:M1_NET_DISPLAY_DISCONNECTED;
    m1_display_set_network_state(target);
}

/* Nonblocking TCP connect with a bounded select. No payload is sent. */
static int probe_one(const char *host)
{
    int fd,result,error=0;
    socklen_t error_size=sizeof(error);
    unsigned long nonblocking=1;
    struct sockaddr_in address;
    struct timeval timeout;
    fd=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if (fd<0) return 0;
    if (ioctl(fd,FIONBIO,&nonblocking)<0) { close(fd); return 0; }
    memset(&address,0,sizeof(address));
    address.sin_family=AF_INET;
    address.sin_port=htons(53);
    address.sin_addr.s_addr=inet_addr(host);
    result=connect(fd,(struct sockaddr *)&address,sizeof(address));
    if (result==0) { close(fd); return 1; }
    {
        fd_set writefds,exceptfds;
        FD_ZERO(&writefds); FD_ZERO(&exceptfds);
        FD_SET(fd,&writefds); FD_SET(fd,&exceptfds);
        timeout.tv_sec=NETWORK_HEALTH_PROBE_TIMEOUT_MS/1000;
        timeout.tv_usec=(NETWORK_HEALTH_PROBE_TIMEOUT_MS%1000)*1000;
        result=select(fd+1,NULL,&writefds,&exceptfds,&timeout);
        if (result>0 && FD_ISSET(fd,&writefds) && !FD_ISSET(fd,&exceptfds) &&
            getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&error_size)==0 && error==0) {
            close(fd); return 1;
        }
    }
    close(fd);
    return 0;
}

static void health_worker(mico_thread_arg_t arg)
{
    uint32_t now,last_probe;
    int connected,success;
    network_health_state_t old,next;
    (void)arg;
    for (;;) {
        connected=wifi_manager_station_ready();
        mico_rtos_lock_mutex(&health_mutex);
        old=health.state;
        network_health_step(&health,connected,-1);
        next=health.state;
        now=mico_rtos_get_time();
        last_probe=health.last_probe_ms;
        mico_rtos_unlock_mutex(&health_mutex);
        if (old!=next) apply_state(next);
        if (!connected || recovery_ota_busy()) { mico_thread_msleep(1000); continue; }
        if (!check_now && last_probe && now-last_probe<NETWORK_HEALTH_PROBE_INTERVAL_MS) {
            mico_thread_msleep(1000); continue;
        }
        check_now=0;
        success=probe_one(probe_hosts[0]);
        if (!success && wifi_manager_station_ready() && !recovery_ota_busy())
            success=probe_one(probe_hosts[1]);
        if (recovery_ota_busy()) { mico_thread_msleep(1000); continue; }
        connected=wifi_manager_station_ready();
        mico_rtos_lock_mutex(&health_mutex);
        old=health.state;
        network_health_step(&health,connected,connected?success:-1);
        health.last_probe_ms=mico_rtos_get_time();
        next=health.state;
        mico_rtos_unlock_mutex(&health_mutex);
        if (old!=next) apply_state(next);
        mico_thread_msleep(1000);
    }
}

OSStatus network_health_init(void)
{
    OSStatus err=mico_rtos_init_mutex(&health_mutex);
    if (err!=kNoErr) return err;
    health.state=NETWORK_NO_WIFI;
    health_ready=1;
    err=mico_rtos_create_thread(&health_thread,MICO_APPLICATION_PRIORITY,"openm1_net_health",
                                health_worker,NETWORK_HEALTH_WORKER_STACK,0);
    if (err!=kNoErr) health_ready=0;
    return err;
}
int network_health_available(void) { return health_ready; }

void network_health_notify_link_down(void)
{
    network_health_state_t old;
    if (!health_ready) return;
    mico_rtos_lock_mutex(&health_mutex);
    old=health.state;
    network_health_step(&health,0,-1);
    mico_rtos_unlock_mutex(&health_mutex);
    if (old!=NETWORK_NO_WIFI) apply_state(NETWORK_NO_WIFI);
}

void network_health_notify_link_ready(void)
{
    if (!health_ready) return;
    check_now=1;
}

void network_health_status_json(char *out,size_t capacity)
{
    network_health_snapshot_t snapshot;
    int link=wifi_manager_station_link();
    char ip[16];
    wifi_manager_station_ip(ip);
    if (health_ready) {mico_rtos_lock_mutex(&health_mutex);snapshot=health;mico_rtos_unlock_mutex(&health_mutex);}
    else memset(&snapshot,0,sizeof(snapshot));
    snprintf(out,capacity,
             "{\"wifi_link\":%s,\"has_ip\":%s,\"internet\":%s,\"state\":\"%s\","
             "\"probe_failures\":%u,\"last_probe_ms\":%lu,\"display_target\":\"%s\","
             "\"red_x_target\":%s,\"display_protocol\":\"pwm\","
             "\"display_protocol_reverse_verified\":true,\"display_hardware_verified\":true,"
             "\"no_internet_auto_red_x_hardware_verified\":false}",
             link?"true":"false",ip[0]?"true":"false",
             snapshot.state==NETWORK_ONLINE?"true":"false",state_name(snapshot.state),
             (unsigned)snapshot.consecutive_failures,(unsigned long)snapshot.last_probe_ms,
             snapshot.state==NETWORK_ONLINE?"wifi_solid":snapshot.state==NETWORK_NO_INTERNET?"wifi_solid":"wifi_blink",
             snapshot.state==NETWORK_NO_INTERNET?"true":"false");
}
