#include "mqtt_proxy.h"
#include "socks5_protocol.h"
#include "mico_socket.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

static int ota_busy;
uint32_t mico_rtos_get_time(void)
{
    struct timeval t;
    gettimeofday(&t,NULL);
    return (uint32_t)((uint64_t)t.tv_sec*1000u+t.tv_usec/1000u);
}
int recovery_ota_busy(void) { return ota_busy; }

typedef struct { int listener,scenario,atyp;uint8_t target[16]; } server_t;
static int read_all(int fd,void *buffer,size_t length)
{
    uint8_t *p=buffer;size_t done=0;int n;
    while(done<length){n=recv(fd,p+done,length-done,0);if(n<=0)return -1;done+=(size_t)n;}
    return 0;
}
static void *server_worker(void *argument)
{
    server_t *server=argument;
    uint8_t b[270];int fd=accept(server->listener,NULL,NULL),auth=server->scenario==1||server->scenario==2;
    assert(fd>=0);
    if (server->scenario==7) {close(fd);return NULL;}
    assert(read_all(fd,b,3)==0 && b[0]==5 && b[1]==1);
    assert(b[2]==(auth?2:0));
    b[0]=5;b[1]=server->scenario==6?0xff:(auth?2:0);
    assert(send(fd,b,2,0)==2);
    if (server->scenario==6) {close(fd);return NULL;}
    if (auth) {
        assert(read_all(fd,b,2)==0 && b[0]==1);
        assert(read_all(fd,b+2,b[1]+1)==0);
        assert(read_all(fd,b+3+b[1],b[2+b[1]])==0);
        assert(b[1]==4 && !memcmp(b+2,"user",4));
        b[0]=1;b[1]=server->scenario==2?1:0;
        assert(send(fd,b,2,0)==2);
        if (server->scenario==2) {close(fd);return NULL;}
    }
    assert(read_all(fd,b,4)==0 && b[0]==5 && b[1]==1 && b[2]==0);
    server->atyp=b[3];
    if (b[3]==1) assert(read_all(fd,b+4,6)==0);
    else if (b[3]==4) assert(read_all(fd,b+4,18)==0);
    else {assert(b[3]==3 && read_all(fd,b+4,1)==0);assert(read_all(fd,b+5,b[4]+2)==0);}
    if (b[3]==4) memcpy(server->target,b+4,16);
    if (server->scenario==5) {sleep(4);close(fd);return NULL;}
    if (server->scenario==4) {b[0]=5;assert(send(fd,b,1,0)==1);close(fd);return NULL;}
    b[0]=5;b[1]=server->scenario==3?5:0;b[2]=0;b[3]=1;
    b[4]=127;b[5]=0;b[6]=0;b[7]=1;b[8]=0;b[9]=0;
    assert(send(fd,b,1,0)==1);usleep(1000);
    assert(send(fd,b+1,9,0)==9);
    close(fd);return NULL;
}
static int start_server(server_t *server,pthread_t *thread)
{
    struct sockaddr_in addr;socklen_t length=sizeof(addr);int enable=1;
    memset(&addr,0,sizeof(addr));addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    server->listener=socket(AF_INET,SOCK_STREAM,0);assert(server->listener>=0);
    setsockopt(server->listener,SOL_SOCKET,SO_REUSEADDR,&enable,sizeof(enable));
    assert(bind(server->listener,(struct sockaddr*)&addr,sizeof(addr))==0);
    assert(getsockname(server->listener,(struct sockaddr*)&addr,&length)==0);
    assert(listen(server->listener,1)==0);
    assert(pthread_create(thread,NULL,server_worker,server)==0);
    return ntohs(addr.sin_port);
}
static void defaults(openm1_config_t *config)
{
    memset(config,0,sizeof(*config));
    strcpy(config->proxy_host,"127.0.0.1");config->proxy_enabled=1;
    strcpy(config->host,"192.0.2.1");config->port=1883;
}
static void run_case(int scenario,const char *target,int expected_atyp,int success)
{
    openm1_config_t config;server_t server;pthread_t thread;
    mqtt_proxy_stage_t stage;int fd=-1,error=0,auth=0,connected=0,result;
    memset(&server,0,sizeof(server));server.scenario=scenario;
    defaults(&config);strcpy(config.host,target);
    if (scenario==1 || scenario==2) {strcpy(config.proxy_username,"user");strcpy(config.proxy_password,"pass");}
    config.proxy_port=(uint16_t)start_server(&server,&thread);
    result=mqtt_proxy_open(&config,&fd,&stage,&error,&auth,&connected);
    assert((result==0)==success);
    if (success) {assert(fd>=0 && connected);close(fd);}
    else assert(fd==-1 && !connected);
    pthread_join(thread,NULL);close(server.listener);
    assert(server.atyp==expected_atyp || scenario==2 || scenario==6);
    if (scenario==1) assert(auth);
    if (scenario==2) assert(stage==PROXY_STAGE_AUTH && error==1);
    if (scenario==3) assert(stage==PROXY_STAGE_TARGET && error==5);
    if (scenario==5) assert(stage==PROXY_STAGE_TIMEOUT);
}
int main(void)
{
    openm1_config_t config; mqtt_proxy_stage_t stage;int fd,error,auth,target;
    run_case(0,"192.0.2.1",1,1);
    run_case(0,"240e:30d:3c4:9801::8f1",4,1);
    run_case(0,"mqtt.example.org",3,1);
    run_case(1,"192.0.2.1",1,1);
    run_case(2,"192.0.2.1",1,0);
    run_case(3,"192.0.2.1",1,0);
    run_case(4,"192.0.2.1",1,0);
    run_case(5,"192.0.2.1",1,0);
    run_case(6,"192.0.2.1",0,0);
    defaults(&config);config.proxy_enabled=0;strcpy(config.host,"127.0.0.1");
    assert(mqtt_proxy_open(&config,&fd,&stage,&error,&auth,&target)<0 && fd==-1);
    assert(stage==PROXY_STAGE_PROTOCOL);
    /* An unreachable proxy must report TCP failure and release its socket. */
    config.proxy_enabled=1;config.proxy_port=1;
    assert(mqtt_proxy_open(&config,&fd,&stage,&error,&auth,&target)<0 && fd==-1);
    defaults(&config);strcpy(config.host,"2001:db8::1");config.proxy_enabled=0;
    assert(mqtt_proxy_open(&config,&fd,&stage,&error,&auth,&target)<0 && fd==-1);
    ota_busy=1;assert(mqtt_proxy_open(&config,&fd,&stage,&error,&auth,&target)<0 && fd==-1);
    puts("MQTT_PROXY_TEST_PASS");return 0;
}
