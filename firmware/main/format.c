#include "format.h"
#include <limits.h>
#include <string.h>

static uint32_t le24(const uint8_t *p)
{
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

// A fixed-rate device advertises its configured rate. A selectable endpoint
// must also implement SET_CUR if it advertises more than one rate.
static uint32_t preferred_rate(const uint8_t *fmt, size_t n)
{
    static const uint32_t rates[] = {384000, 256000, 250000, 192000, 125000, 96000, 48000};
    const unsigned count = fmt[7];
    if (n < (count ? 8 + 3 * count : 14)) return 0;
    for (unsigned i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
        if (!count && rates[i] >= le24(fmt + 8) && rates[i] <= le24(fmt + 11))
            return rates[i];
        for (unsigned j = 0; j < count; j++)
            if (rates[i] == le24(fmt + 8 + 3 * j)) return rates[i];
    }
    return 0;
}

bool find_mic_format(const uint8_t *config, size_t size, bool high_speed, mic_format_t *out)
{
    mic_format_t best = {0}, cur = {0};
    bool as = false, pcm = false, selectable = false, last_data_ep = false;
    for (size_t i = 0; i <= size;) {
        const uint8_t *p = config + i;
        size_t n = i == size ? 0 : p[0];
        if (i != size && (n < 2 || n > size - i)) return false;
        if (i == size || p[1] == 4) {
            if (as && pcm && cur.endpoint && cur.rate > best.rate &&
                (!selectable || cur.rate_control) && cur.interval >= 1 && cur.interval <= 16) {
                uint32_t ticks = 1u << (cur.interval - 1);
                uint32_t hz = high_speed ? 8000 : 1000;
                if ((uint64_t)cur.packet_bytes * hz >= (uint64_t)cur.rate * 2 * ticks)
                    { best = cur; best.rate_control = selectable && cur.rate_control; }
            }
            if (i == size) break;
            as = n >= 9 && p[5] == 1 && p[6] == 2 && p[7] == 0;
            pcm = selectable = last_data_ep = false;
            cur = (mic_format_t){0};
            if (as) { cur.interface = p[2]; cur.alternate = p[3]; }
        } else if (as && p[1] == 0x24 && n >= 3) {
            if (p[2] == 1 && n >= 7) pcm = p[5] == 1 && p[6] == 0;
            if (p[2] == 2 && n >= 8 && p[3] == 1 && p[4] == 1 && p[5] == 2 && p[6] == 16) {
                cur.rate = preferred_rate(p, n);
                selectable = p[7] != 1;
            }
        } else if (as && p[1] == 5 && n >= 7) {
            last_data_ep = (p[2] & 0x80) && (p[3] & 0x33) == 1;
            if (last_data_ep) {
                cur.endpoint = p[2];
                cur.packet_bytes = p[4] | ((uint16_t)p[5] << 8);
                if (cur.packet_bytes > 1024) return false;
                cur.interval = p[6];
            }
        } else if (as && last_data_ep && p[1] == 0x25 && n >= 4 && p[2] == 1) {
            cur.rate_control = p[3] & 1;
        }
        i += n;
    }
    *out = best;
    return best.rate != 0;
}

static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

void make_wav_header(uint8_t h[44], uint32_t rate)
{
    memset(h, 0, 44);
    memcpy(h, "RIFF", 4);
    put32(h + 4, UINT32_MAX); // Unbounded, nonseekable live WAV
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16);
    h[20] = 1; // PCM
    h[22] = 1; // mono
    put32(h + 24, rate);
    put32(h + 28, rate * 2);
    h[32] = 2;
    h[34] = 16;
    memcpy(h + 36, "data", 4);
    put32(h + 40, UINT32_MAX);
}

uint32_t bat_rate(uint32_t input_rate)
{
    if (input_rate == 384000) return 256000;
    return input_rate >= 96000 && input_rate <= 256000 ? input_rate : 0;
}

// Two fractional phases of a 48-tap Kaiser-windowed sinc (beta 7.5,
// cutoff 113 kHz at 384 kHz). Each Q15 phase has unity DC gain.
static const int16_t coefficients[2][FIR_TAPS] = {
    {
        -1, -3, 8, 3, -26, 14, 49, -69, -46, 164, -38, -260,
        248, 256, -582, -6, 931, -637, -1059, 1820, 516, -3994, 2467, 16631,
        16627, 2467, -3994, 516, 1820, -1059, -637, 931, -6, -582, 256, 248,
        -260, -38, 164, -46, -69, 49, 14, -26, 3, 8, -3, -1
    },
    {
        1, -4, 1, 14, -17, -22, 57, 1, -114, 82, 145, -247,
        -70, 455, -203, -582, 720, 418, -1428, 329, 2167, -2248, -2719, 10030,
        19287, 9903, -2651, -2164, 2059, 308, -1320, 381, 647, -515, -177, 389,
        -59, -203, 117, 65, -87, 1, 40, -15, -11, 8, 1, -2
    }
};

void convert_bat_block(bat_filter_t *filter, const int16_t in[BLOCK_SAMPLES],
                       int16_t out[BLOCK_SAMPLES * 2 / 3])
{
    int16_t window[FIR_TAPS - 1 + BLOCK_SAMPLES];
    memcpy(window, filter->history, sizeof(filter->history));
    memcpy(window + FIR_TAPS - 1, in, BLOCK_SAMPLES * sizeof(*in));
    for (unsigned j = 0; j < BLOCK_SAMPLES * 2 / 3; j++) {
        const int16_t *x = window + (3 * j / 2);
        const int16_t *h = coefficients[j & 1];
        int32_t sum = 0;
        for (unsigned k = 0; k < FIR_TAPS; k++) sum += (int32_t)x[k] * h[k];
        int32_t y = (sum + 16384) >> 15;
        out[j] = y > INT16_MAX ? INT16_MAX : y < INT16_MIN ? INT16_MIN : (int16_t)y;
    }
    memcpy(filter->history, in + BLOCK_SAMPLES - (FIR_TAPS - 1), sizeof(filter->history));
}
