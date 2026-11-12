#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Divisible by three for the 384 -> 256 kHz converter.
enum { BLOCK_SAMPLES = 1536, FIR_TAPS = 48 };

typedef struct {
    uint32_t rate;
    uint16_t packet_bytes;
    uint8_t interface, alternate, endpoint, interval;
    bool rate_control;
} mic_format_t;

bool find_mic_format(const uint8_t *config, size_t size, bool high_speed, mic_format_t *out);
void make_wav_header(uint8_t header[44], uint32_t rate);
uint32_t bat_rate(uint32_t input_rate);
typedef struct {
    int16_t history[FIR_TAPS - 1];
} bat_filter_t;
void convert_bat_block(bat_filter_t *filter, const int16_t in[BLOCK_SAMPLES],
                       int16_t out[BLOCK_SAMPLES * 2 / 3]);
