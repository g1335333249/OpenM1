#include "ota_transfer_logic.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>

int main(void)
{
    uint32_t last=1000,received=0,total=9;
    assert(ota_recv_step(3,0,1000,last,received,total)==OTA_RECV_DATA);
    received+=3;last=1000;
    assert(ota_recv_step(-1,EAGAIN,6000,last,received,total)==OTA_RECV_RETRY);
    assert(ota_recv_step(-1,EWOULDBLOCK,11000,last,received,total)==OTA_RECV_RETRY);
    assert(ota_recv_step(-1,EINTR,12000,last,received,total)==OTA_RECV_RETRY);
    assert(ota_recv_step(-1,0,59000,last,received,total)==OTA_RECV_RETRY);
    assert(ota_recv_step(3,0,59001,last,received,total)==OTA_RECV_DATA);
    received+=3;last=59001;
    assert(ota_recv_step(3,0,62000,last,received,total)==OTA_RECV_DATA);
    received+=3;
    assert(ota_recv_step(0,0,62001,last,received,total)==OTA_RECV_COMPLETE); /* No EOF needed. */
    assert(ota_recv_step(0,0,62001,last,3,total)==OTA_RECV_PEER_CLOSED);
    assert(ota_recv_step(-1,EIO,62001,last,3,total)==OTA_RECV_SOCKET_ERROR);
    assert(ota_recv_step(-1,EAGAIN,119001,last,3,total)==OTA_RECV_STALLED);
    assert(ota_recv_step(-1,EAGAIN,999u,UINT32_MAX-500u,3,total)==OTA_RECV_RETRY);
    assert(ota_prepared_expired(121000,1000));
    assert(!ota_prepared_expired(120999,1000));
    assert(ota_prepare_can_begin(0));
    assert(!ota_prepare_can_begin(1));
    assert(!ota_upload_can_begin(0,0,0));
    assert(ota_upload_can_begin(1,1,0));
    assert(!ota_upload_can_begin(1,1,1)); /* URL conflicts with prepared upload. */
    assert(ota_upload_can_begin(0,0,1));
    puts("OTA_TRANSFER_LOGIC_PASS");
}
