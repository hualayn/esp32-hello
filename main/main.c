/*
 * ESP32-S3 + 0.96" SSD1306 OLED：屏幕显示 Hello World，板载 LED 继续 1Hz 闪烁。
 *
 * OLED 接线: VCC=3V3, GND=GND, SCL=GPIO15, SDA=GPIO16 (I2C 地址 0x3C)
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "oled_i2c.h"

#define LED_GPIO  GPIO_NUM_4

static const char *TAG = "app";

void app_main(void)
{
    printf("\n=== ESP32-S3 + OLED Hello World ===\n");

    /* 板载 LED (GPIO4) 1Hz 闪烁 */
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    /* 初始化 OLED */
    int rc = OLED_Init();
    if (rc != 0) {
        ESP_LOGE(TAG, "OLED init failed (code %d) - 请检查接线 (SCL=GPIO15, SDA=GPIO16, VCC, GND)", rc);
        /* OLED 失败时仍保持 LED 闪烁，方便确认程序在运行 */
        for (;;) {
            gpio_set_level(LED_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(500));
            gpio_set_level(LED_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

    /* 清屏并显示 Hello World（水平居中，垂直居中）
     * 12 个字符 x 6px = 72px 宽 -> x = (128-72)/2 = 28
     * 8px 高 -> y = (64-8)/2 = 28 */
    OLED_Clear();
    const char *msg = "Hello World";
    int16_t x = (int16_t)((OLED_WIDTH - (int16_t)strlen(msg) * 6) / 2);
    int16_t y = (int16_t)((OLED_HEIGHT - 8) / 2);
    OLED_ShowString(x, (uint8_t)y, msg);
    OLED_Refresh();
    ESP_LOGI(TAG, "OLED refresh done (fail code: 0x%04X)", (unsigned)oled_refresh_fail);

    /* 主循环：LED 闪烁 */
    for (;;) {
        gpio_set_level(LED_GPIO, 1);
        printf("LED ON\n");
        vTaskDelay(pdMS_TO_TICKS(500));

        gpio_set_level(LED_GPIO, 0);
        printf("LED OFF\n");
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
