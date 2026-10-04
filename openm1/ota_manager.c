#include "mico.h"
#include "mico_system.h"
#include "mico_security.h"
#include "CheckSumUtils.h"
#include "ota_manager.h"
#include <stdlib.h>
#include <string.h>

#define APP_OFFSET 0x75000u
#define OTA_LIMIT 0xB5000u
static char url_copy[192];
static volatile int busy;

static uint16_t app_crc(uint16_t crc, const uint8_t *data, uint32_t len)
{
    uint32_t i;
    int bit;
    for (i = 0; i < len; i++) {
        unsigned c = data[i];
        for (bit = 0; bit < 8; bit++) {
            unsigned a = c & 0x80, b = (crc >> 8) & 0x80;
            c <<= 1;
            crc = (crc << 1) & 0xffff;
            if (a != b) crc ^= 0x1021;
        }
    }
    return crc;
}

static int verify_flash(uint32_t size, uint16_t *image_crc)
{
    uint8_t buf[512], header[8], tail[16], digest[16];
    uint32_t off = APP_OFFSET, left, n, payload_size;
    uint16_t declared, computed = 0;
    md5_context md5;
    CRC16_Context crc;
    if (MicoFlashRead(MICO_PARTITION_OTA_TEMP, &off, header, 8) != kNoErr) return -1;
    payload_size = (uint32_t)header[0] | ((uint32_t)header[1] << 8) | ((uint32_t)header[2] << 16) | ((uint32_t)header[3] << 24);
    declared = (uint16_t)header[4] | ((uint16_t)header[5] << 8);
    if (payload_size != size - APP_OFFSET - 24 || header[4] != header[6] || header[5] != header[7]) return -1;
    left = payload_size;
    while (left) {
        n = left > sizeof(buf) ? sizeof(buf) : left;
        if (MicoFlashRead(MICO_PARTITION_OTA_TEMP, &off, buf, n) != kNoErr) return -1;
        computed = app_crc(computed, buf, n);
        left -= n;
    }
    if (computed != declared) return -1;
    InitMd5(&md5);
    CRC16_Init(&crc);
    off = 0;
    left = size - 16;
    while (left) {
        n = left > sizeof(buf) ? sizeof(buf) : left;
        if (MicoFlashRead(MICO_PARTITION_OTA_TEMP, &off, buf, n) != kNoErr) return -1;
        Md5Update(&md5, buf, n);
        CRC16_Update(&crc, buf, n);
        left -= n;
    }
    Md5Final(&md5, digest);
    CRC16_Final(&crc, image_crc);
    if (MicoFlashRead(MICO_PARTITION_OTA_TEMP, &off, tail, 16) != kNoErr) return -1;
    return memcmp(tail, digest, 16) ? -1 : 0;
}

static void ota_thread(mico_thread_arg_t arg)
{
    char host[96], path[160], header[512], request[320], *slash, *port_text, *length_text;
    struct hostent *entry;
    struct sockaddr_in address;
    mico_logic_partition_t *part;
    uint8_t buf[512];
    uint32_t expected, received = 0, off = 0, n;
    uint16_t crc;
    int fd = -1, port = 80, head_len = 0, count, req_len;
    (void)arg;
    slash = strchr(url_copy + 7, '/');
    if (!slash || (size_t)(slash - url_copy - 7) >= sizeof(host) || strlen(slash) >= sizeof(path)) goto done;
    memcpy(host, url_copy + 7, slash - url_copy - 7);
    host[slash - url_copy - 7] = 0;
    strcpy(path, slash);
    port_text = strchr(host, ':');
    if (port_text) { *port_text++ = 0; port = atoi(port_text); }
    if (!host[0] || port < 1 || port > 65535) goto done;
    entry = gethostbyname(host);
    part = MicoFlashGetInfo(MICO_PARTITION_OTA_TEMP);
    if (!entry || !entry->h_addr_list[0] || !part) goto done;
    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) goto done;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)port);
    memcpy(&address.sin_addr, entry->h_addr_list[0], sizeof(address.sin_addr));
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) goto done;
    req_len = snprintf(request, sizeof(request), "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
    if (req_len < 1 || req_len >= (int)sizeof(request) || send(fd, request, req_len, 0) != req_len) goto done;
    while (head_len < (int)sizeof(header) - 1) {
        if (recv(fd, header + head_len, 1, 0) != 1) goto done;
        head_len++;
        if (head_len >= 4 && !memcmp(header + head_len - 4, "\r\n\r\n", 4)) break;
    }
    if (head_len >= (int)sizeof(header) - 1) goto done;
    header[head_len] = 0;
    if (strncmp(header, "HTTP/1.0 200 ", 13) && strncmp(header, "HTTP/1.1 200 ", 13)) goto done;
    length_text = strstr(header, "Content-Length:");
    if (!length_text) length_text = strstr(header, "content-length:");
    if (!length_text) goto done;
    expected = (uint32_t)strtoul(length_text + 15, NULL, 10);
    if (expected <= APP_OFFSET + 24 || expected > OTA_LIMIT || expected > part->partition_length) goto done;
    if (MicoFlashErase(MICO_PARTITION_OTA_TEMP, 0, part->partition_length) != kNoErr) goto done;
    while (received < expected) {
        n = expected - received;
        if (n > sizeof(buf)) n = sizeof(buf);
        count = recv(fd, buf, n, 0);
        if (count <= 0 || MicoFlashWrite(MICO_PARTITION_OTA_TEMP, &off, buf, count) != kNoErr) goto done;
        received += count;
    }
    close(fd);
    fd = -1;
    if (verify_flash(expected, &crc) != 0) goto done;
    if (mico_ota_switch_to_new_fw(expected - 16, crc) != kNoErr) goto done;
    mico_system_power_perform(mico_system_context_get(), eState_Software_Reset);
done:
    if (fd >= 0) close(fd);
    busy = 0;
    mico_rtos_delete_thread(NULL);
}

int ota_manager_start(const char *url)
{
    if (busy || !url || strncmp(url, "http://", 7) || strlen(url) >= sizeof(url_copy)) return -1;
    strcpy(url_copy, url);
    busy = 1;
    if (mico_rtos_create_thread(NULL, MICO_APPLICATION_PRIORITY, "ota", ota_thread, 0x1800, 0) != kNoErr) {
        busy = 0;
        return -1;
    }
    return 0;
}
