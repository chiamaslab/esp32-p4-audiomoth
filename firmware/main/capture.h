#pragma once
#include "format.h"

typedef struct {
    uint32_t rate, epoch, usb_errors, slow_clients, boot_id;
    uint64_t blocks, samples, epoch_samples;
    int64_t last_packet_us;
} capture_status_t;

void capture_init(void);
void capture_begin(uint32_t rate);
void capture_end(void);
void capture_gap(void);
void capture_write(const uint8_t *pcm, size_t bytes);
capture_status_t capture_status(void);
// -1: stream ended/gap/overrun, 0: not available yet, 1: copied.
int capture_read(uint32_t epoch, uint64_t block, int16_t out[BLOCK_SAMPLES]);
void usb_mic_start(void);
void stream_server_start(void);
