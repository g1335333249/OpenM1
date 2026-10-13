#include "time_sync.h"
#include <string.h>

int time_sync_server_valid(const char *server)
{
    size_t i,n;
    int label=0,numeric_only=1;
    if (!server || !(n=strlen(server)) || n>64 || server[0]=='.' || server[n-1]=='.') return 0;
    for (i=0;i<n;i++) {
        char c=server[i];
        if (c=='.') {if (!label || server[i-1]=='-') return 0;label=0;continue;}
        if (!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-')) return 0;
        if (c<'0' || c>'9') numeric_only=0;
        if (!label && c=='-') return 0;
        label++;
    }
    if (!label || server[n-1]=='-') return 0;
    if (numeric_only) {
        unsigned octet=0,parts=0,digits=0;
        for (i=0;i<=n;i++) {
            char c=server[i];
            if (c=='.' || !c) {
                if (!digits || digits>3 || octet>255) return 0;
                parts++;octet=0;digits=0;
            } else {octet=octet*10u+(unsigned)(c-'0');digits++;}
        }
        return parts==4;
    }
    return 1;
}
static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
int time_sync_parse_reply(const uint8_t *packet,size_t length,const uint8_t token[8],uint32_t *unix_seconds)
{
    uint32_t ntp;
    if (!packet || !token || !unix_seconds || length<48 || (packet[0]&7u)!=4u ||
        (packet[0]>>6)==3u || packet[1]==0 || packet[1]>15 ||
        memcmp(packet+24,token,8)) return 0;
    ntp=be32(packet+40);
    if (ntp<3786825600u || ntp>4291747199u) return 0;
    *unix_seconds=ntp-2208988800u;
    return 1;
}
