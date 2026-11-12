#include "capture.h"
#include <inttypes.h>
#include "esp_chip_info.h"
#include "esp_eth.h"
#include "esp_eth_mac_esp.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static esp_netif_t *netif;

static void on_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    ip_event_got_ip_t *ev = data;
    ESP_LOGI("bat", "BirdNET source http://" IPSTR "/bats.wav   diagnostics http://" IPSTR "/status",
             IP2STR(&ev->ip_info.ip), IP2STR(&ev->ip_info.ip));
}

static void on_link(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == ETHERNET_EVENT_CONNECTED || id == ETHERNET_EVENT_DISCONNECTED)
        ESP_LOGI("bat", "Ethernet link %s", id == ETHERNET_EVENT_CONNECTED ? "up" : "down");
}

static void ethernet_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    netif = esp_netif_new(&cfg);
    ESP_ERROR_CHECK(netif ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(esp_netif_set_hostname(netif, "audiomoth-p4"));
    // P4's default RMII data pins match the NANO. The IP101 supplies REF_CLK.
    eth_esp32_emac_config_t emac = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    emac.smi_gpio.mdc_num = 31;
    emac.smi_gpio.mdio_num = 52;
    emac.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
    emac.clock_config.rmii.clock_gpio = 50;
    eth_mac_config_t mc = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t pc = ETH_PHY_DEFAULT_CONFIG();
    pc.reset_gpio_num = 51;
    pc.phy_addr = ESP_ETH_PHY_ADDR_AUTO;
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac, &mc);
    esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&pc);
    ESP_ERROR_CHECK(mac && phy ? ESP_OK : ESP_ERR_NO_MEM);
    esp_eth_config_t eth = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t handle;
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth, &handle));
    ESP_ERROR_CHECK(esp_netif_attach(netif, esp_eth_new_netif_glue(handle)));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, on_ip, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, on_link, NULL));
    ESP_ERROR_CHECK(esp_eth_start(handle));
}

void app_main(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    ESP_LOGI("bat", "ESP32-P4 v%u.%u, PSRAM %u MiB", chip.revision / 100, chip.revision % 100,
             (unsigned)(esp_psram_get_size() >> 20));
    capture_init();
    ethernet_start();
    stream_server_start();
    usb_mic_start();
    capture_status_t previous = capture_status();
    int64_t last = esp_timer_get_time();
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        capture_status_t s = capture_status();
        int64_t now = esp_timer_get_time();
        uint32_t observed = (s.samples - previous.samples) * 1000000 / (now - last);
        esp_netif_ip_info_t ip = {0};
        esp_netif_get_ip_info(netif, &ip);
        ESP_LOGI("bat", "ip=" IPSTR " input=%" PRIu32 " Hz measured=%" PRIu32 " samples/s total=%" PRIu64
                 " epoch=%" PRIu32 " usb_errors=%" PRIu32 " reader_overruns=%" PRIu32,
                 IP2STR(&ip.ip), s.rate, observed, s.samples, s.epoch, s.usb_errors, s.slow_clients);
        previous = s;
        last = now;
    }
}
