#pragma once
#include <stdint.h>
#define OTA_RECV_TIMEOUT_MS 5000u
#define OTA_UPLOAD_IDLE_TIMEOUT_MS 60000u
#define OTA_PREPARED_TIMEOUT_MS 120000u
typedef enum { OTA_RECV_DATA, OTA_RECV_RETRY, OTA_RECV_COMPLETE, OTA_RECV_STALLED,
               OTA_RECV_PEER_CLOSED, OTA_RECV_SOCKET_ERROR } ota_recv_result_t;
ota_recv_result_t ota_recv_step(int count, int socket_error, uint32_t now,
                                uint32_t last_progress, uint32_t received, uint32_t total);
int ota_prepared_expired(uint32_t now, uint32_t prepared_at);
int ota_prepare_can_begin(int active);
int ota_upload_can_begin(int active,int prepared,int is_url);
