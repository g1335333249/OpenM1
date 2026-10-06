#include "http_activity.h"
#include <string.h>

/* Only an explicit rescue action extends the 60-second boot window. */
int openm1_http_explicit_recovery_activity(const char *method,const char *path)
{
    if (!method || !path) return 0;
    if (!strcmp(method,"GET"))
        return !strcmp(path,"/") || !strcmp(path,"/api/logs/download");
    if (strcmp(method,"POST")) return 0;
    return !strcmp(path,"/api/wifi/settings") || !strcmp(path,"/api/wifi/connect") ||
           !strcmp(path,"/api/wifi/disconnect") || !strcmp(path,"/api/ota/upload") ||
           !strcmp(path,"/api/ota/url") || !strcmp(path,"/api/reboot");
}
