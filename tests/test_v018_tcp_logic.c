#include "ipv6_probe_logic.h"
#include <assert.h>
#include <errno.h>
#include <string.h>

static void expect(const char *want,int cr,int ce,int sr,int wr,int ex,int gr,int checked,int so)
{
    assert(!strcmp(tcp_probe_outcome(cr,ce,sr,wr,ex,gr,checked,so),want));
}
int main(void)
{
    expect("connected",0,0,IPV6_PROBE_NOT_ATTEMPTED,0,0,-1,0,0);
    expect("connected",-1,EINPROGRESS,1,1,0,0,1,0);
    expect("refused",-1,ECONNREFUSED,IPV6_PROBE_NOT_ATTEMPTED,0,0,-1,0,0);
    expect("refused",-1,EINPROGRESS,1,1,0,0,1,ECONNREFUSED);
    expect("no_route",-1,ENETUNREACH,IPV6_PROBE_NOT_ATTEMPTED,0,0,-1,0,0);
    expect("timeout",-1,0,0,0,0,IPV6_PROBE_NOT_ATTEMPTED,0,0);
    expect("timeout",-1,EINPROGRESS,1,1,0,0,1,ETIMEDOUT);
    expect("socket_error",-1,EINPROGRESS,-1,0,0,IPV6_PROBE_NOT_ATTEMPTED,0,0);
    expect("socket_error",-1,EINPROGRESS,1,1,0,-1,0,0);
    expect("socket_error",-1,EINPROGRESS,1,1,0,0,0,0);
    expect("socket_error",-1,0,1,0,1,0,1,0);
    expect("kernel_errno_unavailable",-1,0,1,1,0,0,1,0);
    return 0;
}
