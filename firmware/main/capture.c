#include "capture.h"
#include <string.h>
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

enum { BLOCKS = 256 }; // 1.024 s at 384 kHz, 768 KiB of PSRAM
static int16_t (*ring)[BLOCK_SAMPLES];
static SemaphoreHandle_t lock;
static capture_status_t state;
static size_t partial;

void capture_init(void)
{
    ring = heap_caps_malloc(BLOCKS * sizeof(*ring), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    lock = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(ring && lock ? ESP_OK : ESP_ERR_NO_MEM);
    state.boot_id = esp_random();
}

static void new_epoch(void)
{
    state.epoch++;
    state.epoch_samples = state.blocks = partial = 0;
    state.last_packet_us = 0;
}

void capture_begin(uint32_t rate)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    new_epoch();
    state.rate = rate;
    xSemaphoreGive(lock);
}

void capture_end(void) { capture_begin(0); }

void capture_gap(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    new_epoch();
    state.usb_errors++;
    xSemaphoreGive(lock);
}

void capture_write(const uint8_t *pcm, size_t bytes)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    state.samples += bytes / 2;
    state.epoch_samples += bytes / 2;
    state.last_packet_us = esp_timer_get_time();
    while (bytes) {
        size_t n = (BLOCK_SAMPLES - partial) * 2;
        if (bytes < n) n = bytes;
        memcpy(&ring[state.blocks % BLOCKS][partial], pcm, n);
        partial += n / 2;
        pcm += n;
        bytes -= n;
        if (partial == BLOCK_SAMPLES) { partial = 0; state.blocks++; }
    }
    xSemaphoreGive(lock);
}

capture_status_t capture_status(void)
{
    xSemaphoreTake(lock, portMAX_DELAY);
    capture_status_t snapshot = state;
    xSemaphoreGive(lock);
    return snapshot;
}

int capture_read(uint32_t epoch, uint64_t block, int16_t out[BLOCK_SAMPLES])
{
    int result = 0;
    xSemaphoreTake(lock, portMAX_DELAY);
    if (epoch != state.epoch || !state.rate) result = -1;
    // The writer may already be filling the oldest slot with its next block.
    else if (block < state.blocks && state.blocks - block >= BLOCKS) {
        state.slow_clients++;
        result = -1;
    } else if (block < state.blocks) {
        memcpy(out, ring[block % BLOCKS], sizeof(*ring));
        result = 1;
    }
    xSemaphoreGive(lock);
    return result;
}
