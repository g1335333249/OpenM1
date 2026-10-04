#include "mico.h"
#include "mico_socket.h"
#include "device_info.h"
#include "ota_manager.h"
#include "web_server.h"

static void web_thread(mico_thread_arg_t arg)
{
    int server, client, yes = 1;
    struct sockaddr_in address;
    char request[512], info[320], body[640], response[1024];
    int count, length;
    (void)arg;
    server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server < 0) goto done;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(80);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(server, (struct sockaddr *)&address, sizeof(address)) < 0 || listen(server, 2) < 0) goto close_server;
    while (1) {
        client = accept(server, NULL, NULL);
        if (client < 0) continue;
        count = recv(client, request, sizeof(request) - 1, 0);
        if (count <= 0) { close(client); continue; }
        request[count] = 0;
        if (!strncmp(request, "POST /api/ota ", 14)) {
            char *separator = strstr(request, "\r\n\r\n");
            char *size_header = strstr(request, "Content-Length:");
            unsigned body_len = size_header ? (unsigned)strtoul(size_header + 15, NULL, 10) : 0;
            while (separator && body_len && count < (int)sizeof(request) - 1 &&
                   (unsigned)(count - (separator + 4 - request)) < body_len) {
                int got = recv(client, request + count, sizeof(request) - 1 - count, 0);
                if (got <= 0) break;
                count += got;
                request[count] = 0;
            }
        }
        device_info_json(info, sizeof(info));
        if (!strncmp(request, "GET /api/info ", 14)) {
            snprintf(body, sizeof(body), "%s", info);
        } else if (!strncmp(request, "POST /api/ota ", 14)) {
            char *json = strstr(request, "\r\n\r\n"), *key, *value, *end;
            int started = -1;
            if (json) {
                key = strstr(json + 4, "\"url\"");
                value = key ? strchr(key + 5, ':') : NULL;
                if (value) value = strchr(value, '"');
                end = value ? strchr(value + 1, '"') : NULL;
                if (end && (size_t)(end - value - 1) < 192) {
                    char url[192];
                    memcpy(url, value + 1, end - value - 1);
                    url[end - value - 1] = 0;
                    started = ota_manager_start(url);
                }
            }
            snprintf(body, sizeof(body), "%s", started == 0 ? "{\"status\":\"downloading\"}" : "{\"error\":\"invalid or busy\"}");
        } else {
            snprintf(body, sizeof(body), "<html><body><h1>OpenM1</h1><pre>%s</pre></body></html>", info);
        }
        length = snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %u\r\nConnection: close\r\n\r\n%s", body[0] == '{' ? "application/json" : "text/html", (unsigned)strlen(body), body);
        if (length > 0 && length < (int)sizeof(response)) send(client, response, length, 0);
        close(client);
    }
close_server:
    close(server);
done:
    mico_rtos_delete_thread(NULL);
}

void web_server_start(void)
{
    mico_rtos_create_thread(NULL, MICO_APPLICATION_PRIORITY, "http", web_thread, 0x1000, 0);
}
