#include "http_activity.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    const char *polls[]={"/api/logs","/api/logs?after=4","/api/wifi/status","/api/system/stats",
        "/api/sensors","/api/network/health","/api/display/status","/api/uart/status",
        "/api/ota/status","/api/mqtt/status","/api/homeassistant/status"};
    const char *actions[]={"/api/wifi/settings","/api/wifi/connect","/api/wifi/disconnect",
        "/api/ota/upload","/api/ota/url","/api/reboot"};
    unsigned i;
    for (i=0;i<sizeof(polls)/sizeof(polls[0]);i++)
        assert(!openm1_http_explicit_recovery_activity("GET",polls[i]));
    assert(openm1_http_explicit_recovery_activity("GET","/"));
    assert(openm1_http_explicit_recovery_activity("GET","/api/logs/download"));
    for (i=0;i<sizeof(actions)/sizeof(actions[0]);i++)
        assert(openm1_http_explicit_recovery_activity("POST",actions[i]));
    /* Two minutes of polling has zero deadline extension. */
    for (i=0;i<60;i++) assert(!openm1_http_explicit_recovery_activity("GET","/api/logs?after=100"));
    puts("PASS explicit recovery activity routing");
    return 0;
}
