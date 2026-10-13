#include "ha_ota.h"
#include <stdio.h>
#include <string.h>

int ha_ota_url_valid(const char *url)
{
    const char *host,*path,*end;
    unsigned a,b,c,d,port;int used=0,port_used=0;
    size_t length;
    if (!url || (length=strlen(url))<16 || length>=129 || strncmp(url,"http://",7)) return 0;
    if (strchr(url,'?') || strchr(url,'#') || strchr(url,'@') || strchr(url,' ') ||
        strchr(url,'\r') || strchr(url,'\n')) return 0;
    host=url+7;path=strchr(host,'/');
    if (!path || !path[1]) return 0;
    end=path;
    if (sscanf(host,"%u.%u.%u.%u%n",&a,&b,&c,&d,&used)!=4 ||
        a>255 || b>255 || c>255 || d>255 || used<=0 || host+used>end) return 0;
    if (!(a==10 || (a==172 && b>=16 && b<=31) || (a==192 && b==168))) return 0;
    if (host+used<end) {
        if (host[used]!=':' || sscanf(host+used+1,"%u%n",&port,&port_used)!=1 ||
            port==0 || port>65535 || host+used+1+port_used!=end) return 0;
    }
    for (path++;*path;path++) {
        char c=*path;
        if (!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||
            c=='/'||c=='-'||c=='_'||c=='.'||c=='~'||c=='%'||c=='+'||c=='=')) return 0;
    }
    return 1;
}
int ha_ota_sha256_valid(const char *hex)
{
    unsigned i;
    if (!hex || strlen(hex)!=64) return 0;
    for (i=0;i<64;i++) if (!((hex[i]>='0'&&hex[i]<='9') ||
        (hex[i]>='a'&&hex[i]<='f') || (hex[i]>='A'&&hex[i]<='F'))) return 0;
    return 1;
}
int ha_ota_config_complete(const char *url,const char *version,const char *sha256)
{
    size_t i,n;
    if (!ha_ota_url_valid(url) || !ha_ota_sha256_valid(sha256) || !version ||
        !(n=strlen(version)) || n>=32) return 0;
    for (i=0;i<n;i++) {
        char c=version[i];
        if (!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||
            c=='.'||c=='_'||c=='-')) return 0;
    }
    return 1;
}
int ha_ota_install_payload_valid(const void *payload,size_t length,int retained)
{
    return !retained && payload && length==7 && !memcmp(payload,"INSTALL",7);
}
