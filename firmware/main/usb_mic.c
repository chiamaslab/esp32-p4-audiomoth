#include "capture.h"
#include <inttypes.h>
#include <string.h>
#include "esp_check.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb/usb_host.h"

enum { TRANSFERS = 5, PACKETS = 8 };
static const char *TAG = "audiomoth";
static usb_host_client_handle_t client;
static usb_device_handle_t device;
static usb_transfer_t *control, *iso[TRANSFERS];
static mic_format_t format;
static int pending, address, inflight;
static bool claimed, stopping, removed, retry;
static int64_t retry_at, last_activity;

static void stop_stream(bool retry_same)
{
    if (!stopping) {
        stopping = true;
        retry = retry_same;
        capture_end();
        if (claimed) {
            // Flushing an endpoint returns all submitted transfers via callbacks.
            usb_host_endpoint_halt(device, format.endpoint);
            usb_host_endpoint_flush(device, format.endpoint);
        }
    }
}

static void on_iso(usb_transfer_t *t)
{
    inflight--;
    if (stopping) return;
    if (t->status != USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGW(TAG, "USB transfer status %u", (unsigned)t->status);
        capture_gap();
        stop_stream(true);
        return;
    }
    size_t offset = 0;
    for (int i = 0; i < t->num_isoc_packets; i++) {
        usb_isoc_packet_desc_t *p = &t->isoc_packet_desc[i];
        if (p->status != USB_TRANSFER_STATUS_COMPLETED || p->actual_num_bytes <= 0 ||
            p->actual_num_bytes > p->num_bytes || (p->actual_num_bytes & 1)) {
            capture_gap();
        } else {
            capture_write(t->data_buffer + offset, p->actual_num_bytes);
            last_activity = esp_timer_get_time();
        }
        offset += p->num_bytes;
    }
    if (usb_host_transfer_submit(t) == ESP_OK) inflight++;
    else { capture_gap(); stop_stream(true); }
}

static void start_iso(void)
{
    const int bytes = PACKETS * format.packet_bytes;
    capture_begin(format.rate);
    for (int i = 0; i < TRANSFERS; i++) {
        esp_err_t err = usb_host_transfer_alloc(bytes, PACKETS, &iso[i]);
        if (err == ESP_OK) {
            usb_transfer_t *t = iso[i];
            t->device_handle = device;
            t->bEndpointAddress = format.endpoint;
            t->callback = on_iso;
            t->num_bytes = bytes;
            for (int j = 0; j < PACKETS; j++) t->isoc_packet_desc[j].num_bytes = format.packet_bytes;
            err = usb_host_transfer_submit(t);
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ISO queue %d: %s", i, esp_err_to_name(err));
            stop_stream(true);
            return;
        }
        inflight++;
    }
    ESP_LOGI(TAG, "capturing at %" PRIu32 " Hz", format.rate);
}

static void on_control(usb_transfer_t *t);

static void send_control(uint8_t type, uint8_t request, uint16_t value, uint16_t index,
                         uint16_t length)
{
    usb_setup_packet_t setup = {
        .bmRequestType = type, .bRequest = request,
        .wValue = value, .wIndex = index, .wLength = length
    };
    memcpy(control->data_buffer, &setup, sizeof(setup));
    if (length) {
        for (unsigned i = 0; i < 3; i++)
            control->data_buffer[sizeof(setup) + i] = format.rate >> (8 * i);
    }
    control->device_handle = device;
    control->bEndpointAddress = 0;
    control->callback = on_control;
    control->num_bytes = sizeof(setup) + length;
    esp_err_t err = usb_host_transfer_submit_control(client, control);
    if (err == ESP_OK) inflight++;
    else { ESP_LOGE(TAG, "submit control: %s", esp_err_to_name(err)); stop_stream(true); }
}

static void on_control(usb_transfer_t *t)
{
    inflight--;
    if (stopping) return;
    if (t->status != USB_TRANSFER_STATUS_COMPLETED) {
        ESP_LOGE(TAG, "control request 0x%02x status %u", t->data_buffer[1], (unsigned)t->status);
        stop_stream(true);
        return;
    }
    if (t->data_buffer[1] == 0x0b && format.rate_control)
        send_control(0x22, 0x01, 0x0100, format.endpoint, 3); // UAC1 SET_CUR
    else start_iso();
}

static void open_mic(int addr)
{
    if (usb_host_device_open(client, addr, &device) != ESP_OK) return;
    address = addr;
    stopping = removed = retry = false;
    last_activity = esp_timer_get_time();
    const usb_config_desc_t *config = NULL;
    const usb_device_desc_t *desc = NULL;
    usb_device_info_t info;
    if (usb_host_get_device_descriptor(device, &desc) != ESP_OK ||
        usb_host_device_info(device, &info) != ESP_OK ||
        usb_host_get_active_config_descriptor(device, &config) != ESP_OK) {
        stop_stream(true);
        return;
    }
    ESP_LOGI(TAG, "USB %04x:%04x, address %d, %s speed", desc->idVendor, desc->idProduct,
             addr, info.speed == USB_SPEED_HIGH ? "high" : "full/low");
    if (!find_mic_format((const uint8_t *)config, config->wTotalLength,
                         info.speed == USB_SPEED_HIGH, &format)) {
        ESP_LOGW(TAG, "No supported mono 16-bit UAC1 input. Check AudioMoth microphone mode/rate.");
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, config, config->wTotalLength, ESP_LOG_INFO);
        stop_stream(false);
        return;
    }
    ESP_LOGI(TAG, "interface %u alt %u ep 0x%02x MPS %u interval %u, %" PRIu32 " Hz",
             format.interface, format.alternate, format.endpoint, format.packet_bytes,
             format.interval, format.rate);
    esp_err_t err = usb_host_interface_claim(client, device, format.interface, format.alternate);
    claimed = err == ESP_OK;
    if (claimed) err = usb_host_transfer_alloc(sizeof(usb_setup_packet_t) + 3, 0, &control);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "claim/setup: %s", esp_err_to_name(err));
        stop_stream(true);
        return;
    }
    // Claim allocates host endpoint pipes. The device also needs SET_INTERFACE.
    send_control(0x01, 0x0b, format.alternate, format.interface, 0);
}

static void close_mic(void)
{
    for (int i = 0; i < TRANSFERS; i++) {
        if (iso[i]) usb_host_transfer_free(iso[i]);
        iso[i] = NULL;
    }
    if (control) usb_host_transfer_free(control);
    control = NULL;
    if (claimed) ESP_ERROR_CHECK_WITHOUT_ABORT(usb_host_interface_release(client, device, format.interface));
    ESP_ERROR_CHECK_WITHOUT_ABORT(usb_host_device_close(client, device));
    if (retry && !removed && !pending) {
        pending = address;
        retry_at = esp_timer_get_time() + 1000000;
    }
    device = NULL;
    claimed = stopping = false;
}

static void on_device(const usb_host_client_event_msg_t *event, void *unused)
{
    if (event->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        pending = event->new_dev.address;
        retry_at = 0;
    } else if (event->event == USB_HOST_CLIENT_EVENT_DEV_GONE && event->dev_gone.dev_hdl == device) {
        removed = true;
        stop_stream(false);
        ESP_LOGI(TAG, "microphone disconnected");
    }
}

static void client_task(void *unused)
{
    uint8_t initial[1];
    int count = 0;
    if (usb_host_device_addr_list_fill(1, initial, &count) == ESP_OK && count) pending = initial[0];
    for (;;) {
        usb_host_client_handle_events(client, pdMS_TO_TICKS(50));
        if (device && !stopping && esp_timer_get_time() - last_activity > 2500000) {
            ESP_LOGW(TAG, "USB input stalled; restarting interface");
            capture_gap();
            stop_stream(true);
        }
        if (stopping && !inflight) close_mic();
        if (pending && !device && esp_timer_get_time() >= retry_at) {
            int next = pending;
            pending = 0;
            open_mic(next);
        }
    }
}

static void usb_daemon(void *unused)
{
    for (;;) {
        uint32_t events;
        usb_host_lib_handle_events(portMAX_DELAY, &events);
    }
}

void usb_mic_start(void)
{
    // Default peripheral on the P4 is the high-speed controller wired to USB-A.
    usb_host_config_t host = {.intr_flags = ESP_INTR_FLAG_LEVEL1};
    ESP_ERROR_CHECK(usb_host_install(&host));
    usb_host_client_config_t cfg = {
        .is_synchronous = false, .max_num_event_msg = 8, .async.client_event_callback = on_device
    };
    ESP_ERROR_CHECK(usb_host_client_register(&cfg, &client));
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(usb_daemon, "usb_lib", 4096, NULL, 17, NULL, 1) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore(client_task, "usb_mic", 6144, NULL, 16, NULL, 1) == pdPASS
                    ? ESP_OK : ESP_ERR_NO_MEM);
}
