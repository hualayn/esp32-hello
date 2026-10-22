idf_component_register(SRCS "main.c" "oled/u8g2_display.c" "wifi.c" "web.c"
                       INCLUDE_DIRS "oled"
                       REQUIRES u8g2 esp_wifi esp_netif esp_event esp_http_server esp_driver_i2c nvs_flash console)
