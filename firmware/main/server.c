#include "capture.h"
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

static SemaphoreHandle_t slots;

static bool send_all(int fd, const void *data, size_t n)
{
    const uint8_t *p = data;
    while (n) {
        ssize_t written = send(fd, p, n, 0);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return false;
        p += written;
        n -= written;
    }
    return true;
}

static void reply(int fd, const char *status, const char *type, const char *body, bool head)
{
    char hdr[200];
    int n = snprintf(hdr, sizeof(hdr), "HTTP/1.0 %s\r\nContent-Type: %s\r\n"
                     "Content-Length: %u\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",
                     status, type, (unsigned)strlen(body));
    if (send_all(fd, hdr, n) && !head) send_all(fd, body, strlen(body));
}

static void get_status(int fd, bool head)
{
    capture_status_t s = capture_status();
    char json[640];
    snprintf(json, sizeof(json),
             "{\"boot_id\":%" PRIu32 ",\"epoch\":%" PRIu32 ",\"input_hz\":%" PRIu32
             ",\"bat_hz\":%" PRIu32 ",\"samples_total\":%" PRIu64 ",\"epoch_samples\":%" PRIu64
             ",\"next_block\":%" PRIu64 ",\"samples_per_block\":%u,\"usb_errors\":%" PRIu32
             ",\"reader_overruns\":%" PRIu32 ",\"packet_age_ms\":%" PRId64
             ",\"uptime_us\":%" PRId64 "}\n",
             s.boot_id, s.epoch, s.rate, bat_rate(s.rate), s.samples, s.epoch_samples, s.blocks,
             BLOCK_SAMPLES, s.usb_errors, s.slow_clients,
             s.last_packet_us ? (esp_timer_get_time() - s.last_packet_us) / 1000 : (int64_t)-1,
             esp_timer_get_time());
    reply(fd, "200 OK", "application/json", json, head);
}

typedef struct {
    int16_t native[BLOCK_SAMPLES], filtered[BLOCK_SAMPLES * 2 / 3];
    bat_filter_t filter;
} stream_buffers_t;

static void serve_audio(int fd, bool bats, bool head)
{
    capture_status_t s = capture_status();
    uint32_t rate = bats ? bat_rate(s.rate) : s.rate;
    if (!rate || !s.blocks || esp_timer_get_time() - s.last_packet_us > 500000) {
        reply(fd, "503 Service Unavailable", "text/plain",
              "Waiting for microphone audio. For bat audio set the AudioMoth to 384 kHz. See /status.\n", head);
        return;
    }
    stream_buffers_t *buf = head ? NULL : calloc(1, sizeof(*buf));
    if (!head && !buf) { reply(fd, "503 Service Unavailable", "text/plain", "Out of memory\n", false); return; }
    char header[400];
    int n = snprintf(header, sizeof(header),
                     "HTTP/1.0 200 OK\r\nContent-Type: audio/wav\r\nConnection: close\r\n"
                     "Cache-Control: no-store\r\nAccept-Ranges: none\r\nX-Boot-ID: %" PRIu32
                     "\r\nX-Capture-Epoch: %" PRIu32 "\r\nX-First-Input-Sample: %" PRIu64
                     "\r\nX-Input-Hz: %" PRIu32 "\r\n\r\n",
                     s.boot_id, s.epoch, s.blocks * BLOCK_SAMPLES, s.rate);
    uint8_t wav[44];
    make_wav_header(wav, rate);
    if (send_all(fd, header, n) && !head && send_all(fd, wav, sizeof(wav))) {
        uint64_t cursor = s.blocks;
        while (true) {
            int ready = capture_read(s.epoch, cursor, buf->native);
            if (ready < 0) break;
            if (!ready) { vTaskDelay(pdMS_TO_TICKS(2)); continue; }
            if (rate != s.rate) {
                convert_bat_block(&buf->filter, buf->native, buf->filtered);
                if (!send_all(fd, buf->filtered, sizeof(buf->filtered))) break;
            } else if (!send_all(fd, buf->native, sizeof(buf->native))) break;
            cursor++;
        }
    }
    free(buf);
}

static void handle_request(int fd)
{
    char request[2048];
    size_t used = 0;
    while (used < sizeof(request) - 1) {
        int n = recv(fd, request + used, sizeof(request) - used - 1, 0);
        if (n <= 0) return;
        used += n;
        request[used] = 0;
        if (strstr(request, "\r\n\r\n")) break;
    }
    char method[8], path[96];
    if (!strstr(request, "\r\n\r\n") || sscanf(request, "%7s %95s", method, path) != 2) {
        reply(fd, "400 Bad Request", "text/plain", "Invalid request\n", false);
        return;
    }
    bool head = strcmp(method, "HEAD") == 0;
    if (!head && strcmp(method, "GET"))
        reply(fd, "405 Method Not Allowed", "text/plain", "Use GET or HEAD\n", false);
    else if (!strcmp(path, "/status") || !strcmp(path, "/")) get_status(fd, head);
    else if (!strcmp(path, "/native.wav")) serve_audio(fd, false, head);
    else if (!strcmp(path, "/bats.wav")) serve_audio(fd, true, head);
    else reply(fd, "404 Not Found", "text/plain", "Use /status, /native.wav or /bats.wav\n", head);
}

static void client_task(void *arg)
{
    int fd = (intptr_t)arg;
    struct timeval timeout = {.tv_sec = 2};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    handle_request(fd);
    close(fd);
    xSemaphoreGive(slots);
    vTaskDelete(NULL);
}

static void server_task(void *unused)
{
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_port = htons(80),
                               .sin_addr.s_addr = htonl(INADDR_ANY)};
    for (;;) {
        int server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (server >= 0) {
            int yes = 1;
            setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
            if (bind(server, (struct sockaddr *)&addr, sizeof(addr)) == 0 && listen(server, 5) == 0) {
                for (;;) {
                    int fd = accept(server, NULL, NULL);
                    if (fd < 0) break;
                    if (xSemaphoreTake(slots, 0) != pdTRUE) {
                        reply(fd, "503 Service Unavailable", "text/plain", "All stream slots busy\n", false);
                        close(fd);
                    } else if (xTaskCreatePinnedToCore(client_task, "wav_client", 14336, (void *)(intptr_t)fd,
                                                       7, NULL, 0) != pdPASS) {
                        xSemaphoreGive(slots);
                        close(fd);
                    }
                }
            }
            close(server);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void stream_server_start(void)
{
    slots = xSemaphoreCreateCounting(4, 4);
    ESP_ERROR_CHECK(slots && xTaskCreate(server_task, "wav_listen", 4096, NULL, 6, NULL) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
}
