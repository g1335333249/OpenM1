#include "ota_transfer_logic.h"
#include <errno.h>
ota_recv_result_t ota_recv_step(int count, int socket_error, uint32_t now,
                                uint32_t last_progress, uint32_t received, uint32_t total)
{
    if (received>=total) return OTA_RECV_COMPLETE;
    if (count>0) return OTA_RECV_DATA;
    if (count==0) return OTA_RECV_PEER_CLOSED;
    /* The pinned MOC recv forwards to opaque lwIP. Some builds leave errno at
     * zero on SO_RCVTIMEO; retry that case only within the bounded idle time. */
    if (socket_error==EINTR || socket_error==EAGAIN || socket_error==EWOULDBLOCK || !socket_error)
        return (uint32_t)(now-last_progress)>=OTA_UPLOAD_IDLE_TIMEOUT_MS?
               OTA_RECV_STALLED:OTA_RECV_RETRY;
    return OTA_RECV_SOCKET_ERROR;
}
int ota_prepared_expired(uint32_t now, uint32_t prepared_at)
{
    return (uint32_t)(now-prepared_at)>=OTA_PREPARED_TIMEOUT_MS;
}
int ota_prepare_can_begin(int active) { return !active; }
int ota_upload_can_begin(int active,int prepared,int is_url)
{
    return is_url?!active:(active && prepared);
}
