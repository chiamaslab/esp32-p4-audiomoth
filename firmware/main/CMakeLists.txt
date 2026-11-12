idf_component_register(SRCS "main.c" "usb_mic.c" "capture.c" "format.c" "server.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES usb esp_eth esp_netif esp_event esp_timer esp_psram lwip)
