/*
 * ESP32-S3 + 0.96" SSD1306 OLED + WiFi AP (软热点) + Web 输入。
 *
 * - ESP32 作为 AP 广播 SSID esp32-test (WPA2, 密码 00000000)
 * - 手机连上后打开 http://192.168.4.1，输入文字并提交
 * - 提交的文字（支持中文，u8g2 字库）显示在 OLED 屏幕上
 *
 * OLED 接线: VCC=3V3, GND=GND, SCL=GPIO15, SDA=GPIO16 (I2C 地址 0x3C)
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "u8g2_display.h"
#include "wifi.h"
#include "web.h"

#define WIFI_SSID "esp32-test"
#define WIFI_PASS "00000000"

static const char *TAG = "app";

void app_main(void)
{
    printf("\n=== ESP32-S3 + OLED (u8g2) + WiFi AP + Web ===\n");

    /* 初始化 OLED（u8g2 驱动，含中文字库） */
    if (oled_u8g2_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "OLED init failed - 请检查接线 (SCL=GPIO15, SDA=GPIO16, VCC, GND)");
        for (;;)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    /* 启动 WiFi 热点 (AP 模式)，成功后手机/电脑可连接 esp32-test */
    int wrc = wifi_ap_start(WIFI_SSID, WIFI_PASS);
    if (wrc != 0)
    {
        ESP_LOGE(TAG, "WiFi AP start failed (code %d)", wrc);
    }

    /* 初始显示：SSID + 网关地址，第三行等待 Web 输入 */
    oled_show_text("");

    /* 启动 Web 服务器：GET / 返回输入页，POST /submit 更新屏幕 */
    if (web_server_start() != 0)
    {
        ESP_LOGE(TAG, "web server start failed");
    }

    /* 主循环：空闲，Web 回调负责显示更新 */
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
