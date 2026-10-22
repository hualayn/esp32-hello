/*
 * ESP32-S3 + 0.96" SSD1306 OLED + WiFi AP (软热点) + Web 输入 + 串口控制台。
 *
 * - ESP32 作为 AP 广播 SSID esp32-test (WPA2, 密码 00000000)
 * - 手机连上后打开 http://192.168.4.1，输入文字并提交
 * - 提交的文字（支持中文，u8g2 字库）显示在 OLED 屏幕上
 * - 串口控制台（ESP32-S3 原生 USB 口 = USB-Serial-JTAG）：输入 "oled <文字>" 回车，直接显示到屏幕
 *
 * 注意: IDF 5.5 的 esp_console 有个 bug —— linenoise 的 sanitize() 用 isprint()
 * 过滤命令行，而 C 语言 locale 下 isprint 只认 ASCII，会把 UTF-8 中文参数全部
 * 剥掉（回显完整、执行时却只剩命令词）。本地 IDF 的
 * components/console/linenoise/linenoise.c sanitize() 已打补丁保留 >=0x80 字节。
 *
 * OLED 接线: VCC=3V3, GND=GND, SCL=GPIO15, SDA=GPIO16 (I2C 地址 0x3C)
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_console.h"
#include "u8g2_display.h"
#include "wifi.h"
#include "web.h"

#define WIFI_SSID "esp32-test"
#define WIFI_PASS "00000000"

static const char *TAG = "app";

/* ---------- 串口控制台命令 ----------
 * 在终端（idf.py monitor / PuTTY 115200 8N1）里输入:
 *     oled 测试
 * 回车后，OLED 立即显示"测试"（UTF-8，最多两行自动换行）。
 * 注意：REPL 运行在 esp_console 自己的任务里，与 httpd 任务（网页提交）
 * 理论上可能同时调 oled_show_text —— u8g2 非线程安全，学习项目里可忽略。
 */
static int cmd_oled(int argc, char **argv)
{
    if (argc < 2)
    {
        ESP_LOGW(TAG, "用法: oled <文字>");
        return -1;
    }
    /* 把剩余参数用空格拼成一个字符串 */
    char buf[128];
    int n = 0;
    for (int i = 1; i < argc && n + (int)strlen(argv[i]) + 2 < (int)sizeof(buf); i++)
    {
        if (n > 0)
        {
            buf[n++] = ' ';
        }
        n += snprintf(buf + n, sizeof(buf) - n, "%s", argv[i]);
    }
    buf[n] = '\0';
    oled_show_text(buf);
    ESP_LOGI(TAG, "OLED <- %s", buf);
    return 0;
}

static const esp_console_cmd_t s_commands[] = {
    {
        .command = "oled",
        .help = "OLED 显示文字: oled <文字>\n例: oled 测试",
        .func = &cmd_oled,
    },
    { 0, 0, 0, 0 },  /* 结束标记 */
};

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

    /* 串口控制台（ESP32-S3 原生 USB 口 = USB-Serial-JTAG）：
     * 输入 "oled 测试" 回车即可显示到屏幕。
     * IDF 5.5 新 API：先注册命令，再创建 USB REPL（内部自动完成 console 初始化 + help 命令），
     * 最后 esp_console_start_repl 唤醒 REPL 任务。 */
    esp_console_cmd_register(&s_commands[0]);
    esp_console_dev_usb_serial_jtag_config_t dev_cfg = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    esp_console_repl_t *repl = NULL;
    if (esp_console_new_repl_usb_serial_jtag(&dev_cfg, &repl_cfg, &repl) != ESP_OK)
    {
        ESP_LOGE(TAG, "console repl init failed");
    }
    else if (esp_console_start_repl(repl) != ESP_OK)   /* 必须调用：REPL 任务在此之前阻塞等待 */
    {
        ESP_LOGE(TAG, "console repl start failed");
    }

    /* 主循环：空闲，Web 回调和串口命令负责显示更新 */
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
