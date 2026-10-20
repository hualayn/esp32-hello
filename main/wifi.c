/**
 * @file    wifi.c
 * @brief   ESP32-S3 soft-AP implementation (see wifi.h).
 *
 *          Default AP netif:
 *            AP IP          192.168.4.1
 *            client pool    192.168.4.2 - 192.168.4.9 (built-in DHCP)
 */
#include "wifi.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "nvs_flash.h"

static const char *TAG = "wifi";

static char s_ip[16] = "192.168.4.1";
static int s_clients = 0;
static SemaphoreHandle_t s_ap_sem;
static esp_netif_t *s_ap_netif;

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_AP_START) {
        esp_netif_ip_info_t ip_info;
        if (s_ap_netif != NULL && esp_netif_get_ip_info(s_ap_netif, &ip_info) == ESP_OK) {
            snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&ip_info.ip));
        }
        ESP_LOGI(TAG, "AP started, IP %s (clients get 192.168.4.2+)", s_ip);
        if (s_ap_sem != NULL) {
            xSemaphoreGive(s_ap_sem);
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        const wifi_event_ap_staconnected_t *ev = (const wifi_event_ap_staconnected_t *)data;
        s_clients++;
        ESP_LOGI(TAG, "client %02x:%02x:%02x:%02x:%02x:%02x connected (%d total)",
                 ev->mac[0], ev->mac[1], ev->mac[2], ev->mac[3], ev->mac[4], ev->mac[5],
                 s_clients);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STADISCONNECTED) {
        s_clients--;
        ESP_LOGI(TAG, "client disconnected (%d total)", s_clients);
    }
}

int wifi_ap_start(const char *ssid, const char *pass)
{
    /* NVS stores the WiFi/PHY calibration data - the driver needs it. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return -1;
    }

    /* Netif subsystem + default event loop (the wifi driver posts its
     * events there; both must exist before any esp_event_handler_register). */
    if (esp_netif_init() != ESP_OK || esp_event_loop_create_default() != ESP_OK) {
        ESP_LOGE(TAG, "netif_init / event_loop_create_default failed");
        return -1;
    }

    s_ap_sem = xSemaphoreCreateBinary();
    if (s_ap_sem == NULL) {
        return -1;
    }
    if (esp_event_handler_register(ESP_EVENT_ANY_BASE, ESP_EVENT_ANY_ID, on_event, NULL) != ESP_OK) {
        ESP_LOGE(TAG, "event handler register failed");
        return -1;
    }

    s_ap_netif = esp_netif_create_default_wifi_ap();
    if (s_ap_netif == NULL) {
        ESP_LOGE(TAG, "create default wifi AP netif failed");
        return -1;
    }

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&init_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed");
        return -1;
    }

    wifi_config_t cfg = { 0 };
    cfg.ap.ssid_len = (uint8_t)strnlen(ssid, sizeof(cfg.ap.ssid) - 1);
    memcpy(cfg.ap.ssid, ssid, cfg.ap.ssid_len);
    if (pass != NULL && strlen(pass) >= 8 && strlen(pass) <= 63) {
        memcpy(cfg.ap.password, pass, strlen(pass));
        cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        cfg.ap.authmode = WIFI_AUTH_OPEN;
    }
    cfg.ap.channel = 1;
    cfg.ap.beacon_interval = 100;
    cfg.ap.max_connection = 4;

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_AP, &cfg);
    }
    if (err == ESP_OK) {
        err = esp_wifi_start();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi set_mode/set_config/start failed: %s", esp_err_to_name(err));
        return -1;
    }

    if (xSemaphoreTake(s_ap_sem, pdMS_TO_TICKS(10000)) != pdTRUE) {
        ESP_LOGE(TAG, "AP did not start within 10 s");
        return -2;
    }
    return 0;
}

const char *wifi_ap_ip(void)
{
    return s_ip;
}

int wifi_ap_client_count(void)
{
    return s_clients;
}
