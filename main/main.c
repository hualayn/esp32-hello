/*
 * ESP32-S3 + 0.96" SSD1306 OLED + WiFi AP (软热点) + Web 输入 + 串口控制台。
 *
 * - ESP32 作为 AP 广播 SSID esp32-test (WPA2, 密码 00000000)
 * - 手机连上后打开 http://192.168.4.1，输入文字并提交
 * - 提交的文字（支持中文，u8g2 字库）显示在 OLED 屏幕上
 * - 串口控制台（ESP32-S3 原生 USB 口 = USB-Serial-JTAG）：输入 "oled <文字>" 回车，直接显示到屏幕
 * - 热敏电阻温度模块三档状态（完全由 AO ADC 温度决定，DO 仅日志参考）：
 *   - temp_c ≥ 35°C        → 超温（LED 常亮 + OLED 显示"超温"）
 *   - 25°C ≤ temp_c < 35°C → 正常（LED 熄灭 + OLED 显示"正常"）
 *   - temp_c < 25°C        → 低温（LED 闪烁 + OLED 显示"低温"）
 * - 外接按键 ×2：GPIO10 / GPIO12（各一脚接对应引脚、一脚接 GND，内部上拉，按下=低电平）：
 *   任一按下 → 翻转板载 LED（正常温度档下生效）+ OLED 显示"按钮10按下啦！"/"按钮12按下啦！"3 秒
 *
 * 注意: IDF 5.5 的 esp_console 有个 bug —— linenoise 的 sanitize() 用 isprint()
 * 过滤命令行，而 C 语言 locale 下 isprint 只认 ASCII，会把 UTF-8 中文参数全部
 * 剥掉（回显完整、执行时却只剩命令词）。本地 IDF 的
 * components/console/linenoise/linenoise.c sanitize() 已打补丁保留 >=0x80 字节。
 *
 * OLED 接线: VCC=3V3, GND=GND, SCL=GPIO15, SDA=GPIO16 (I2C 地址 0x3C)
 * 温度模块接线: DO=GPIO5, AO=GPIO4 (ADC), VCC=3V3, GND=GND
 * 按键接线: 按键1 一脚=GPIO10，按键2 一脚=GPIO12，另一脚均接 GND（内部上拉，无需外部电阻）
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_console.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "u8g2_display.h"
#include "wifi.h"
#include "web.h"

#define WIFI_SSID "esp32-test"
#define WIFI_PASS "00000000"

/* 热敏电阻温度模块：DO 接 GPIO5，AO 接 GPIO4（ADC 采样） */
#define PIN_TEMP_DO  GPIO_NUM_5
#define PIN_LED      GPIO_NUM_48   /* 板载蓝色 LED（DevKitC-1：高电平点亮） */
#define PIN_BTN1     GPIO_NUM_10   /* 外接按键1：一脚接 GPIO10、一脚接 GND（内部上拉，按下为低电平） */
#define PIN_BTN2     GPIO_NUM_12   /* 外接按键2：一脚接 GPIO12、一脚接 GND（内部上拉，按下为低电平） */
#define BTN_PRESS_MS 3000          /* 按下后 OLED 提示保持时长 */

/* AO 引脚（ADC） */
#define PIN_TEMP_AO  GPIO_NUM_4
#define ADC_UNIT     ADC_UNIT_1
#define ADC_CHANNEL  ADC_CHANNEL_3   /* GPIO4 = ADC1_CH3 */
#define ADC_ATTEN    ADC_ATTEN_DB_12 /* 0~3.3V 满量程 */

/* 比较器输出极性：本模块实测 "低电平 = 超温"（NTC 热敏电阻温度升高阻值下降，
 * AO 分压随之下降，比较器在 AO < 参考电压时输出低电平）。
 * 如果你的模块是"超温输出高电平"，把这里改成 0 即可。
 * 注意：当前三档判定完全基于 AO 温度，DO 电平仅用于日志参考。 */

/* NTC 热敏电阻标定曲线：
 * 模块型号不同曲线略有差异，这里用典型 B 值公式近似：
 *   R(T) = R25 * exp(B * (1/T - 1/T25))
 * 分压公式（AO 端）：Vout = Vcc * Rntc / (Rfixed + Rntc)
 * Vcc = 3.3V，Rfixed = 10kΩ（典型值）。
 * 用 AO 采样电压反算 NTC 阻值，再反算温度（开尔文）。
 */
#define NTC_R25    10000.0f  /* 25°C 标称阻值 10k */
#define NTC_BETA   3950.0f   /* B 值（典型 NTC） */
#define NTC_RFIXED 10000.0f  /* 分压固定电阻 */
#define NTC_VCC    3.3f      /* 模块供电电压 */
#define NTC_T25_K  298.15f   /* 25°C = 298.15K */

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
    { 0 },  /* 结束标记 */
};

/* ---------- ADC 初始化（AO 引脚） ---------- */
static adc_oneshot_unit_handle_t adc1_handle;

static void temp_adc_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = ADC_UNIT };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc1_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL, &chan_cfg));
}

/* 读 AO 原始 ADC 值，返回摄氏温度；失败返回 NAN */
static float temp_read_celsius(void)
{
    int raw = 0;
    if (adc_oneshot_read(adc1_handle, ADC_CHANNEL, &raw) != ESP_OK)
    {
        return NAN;
    }
    /* ADC → 电压（ESP32-S3 12bit, 0~4095 → 0~3.3V） */
    float vout = (raw / 4095.0f) * NTC_VCC;

    /* 反算 NTC 阻值：Vout = Vcc * Rntc / (Rfixed + Rntc) → Rntc = Rfixed * Vout / (Vcc - Vout) */
    if (vout >= NTC_VCC - 0.01f || vout <= 0.01f)
    {
        return NAN;  /* 分压异常（开路/短路） */
    }
    float rntc = NTC_RFIXED * vout / (NTC_VCC - vout);

    /* NTC B 值公式反算温度：T = 1 / (1/T25 + ln(R/R25)/B) */
    float t_k = 1.0f / (1.0f / NTC_T25_K + logf(rntc / NTC_R25) / NTC_BETA);
    return t_k - 273.15f;  /* 开尔文 → 摄氏 */
}

/* 三档状态 */
typedef enum {
    TEMP_STATE_LOW,      /* 低温（冰触碰） */
    TEMP_STATE_NORMAL,   /* 常温 */
    TEMP_STATE_OVER,     /* 超温报警 */
} temp_state_t;

/* ---------- 按键（外接，250ms 轮询 + 连续 3 次一致去抖 ≈ 750ms） ---------- */
typedef struct {
    gpio_num_t pin;
    int last_level;
    int cnt;
    int latched;   /* 按下锁存：一次按压只触发一次 */
} btn_state_t;

/* 更新一个按键的去抖状态；按下(低)确认的当刻返回 1，松开(高)确认后解锁 */
static int btn_update(btn_state_t *b)
{
    int lvl = gpio_get_level(b->pin);
    if (lvl == b->last_level) { b->cnt++; }
    else { b->last_level = lvl; b->cnt = 1; }
    if (lvl == 0 && b->cnt == 3 && !b->latched)
    {
        b->latched = 1;
        return 1;
    }
    if (lvl == 1 && b->cnt == 3)
    {
        b->latched = 0;
    }
    return 0;
}

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

    /* ---------- 温度监控主循环 ---------- */
    gpio_set_direction(PIN_TEMP_DO, GPIO_MODE_INPUT);
    gpio_set_direction(PIN_LED, GPIO_MODE_OUTPUT);
    gpio_set_level(PIN_LED, 0);

    /* 外接按键：输入 + 内部上拉，按下 = 低电平 */
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << PIN_BTN1) | (1ULL << PIN_BTN2),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn_cfg));

    temp_adc_init();  /* 初始化 AO 引脚 ADC */

    int last_level = gpio_get_level(PIN_TEMP_DO);
    int same_count = 1;
    temp_state_t state = TEMP_STATE_NORMAL;   /* 当前档位 */
    TickType_t last_tick = xTaskGetTickCount();
    TickType_t last_refresh = xTaskGetTickCount();

    /* 按键状态（250ms 轮询 + 连续 3 次一致去抖 ≈ 750ms） */
    btn_state_t btn1 = { .pin = PIN_BTN1, .last_level = gpio_get_level(PIN_BTN1), .cnt = 1, .latched = 0 };
    btn_state_t btn2 = { .pin = PIN_BTN2, .last_level = gpio_get_level(PIN_BTN2), .cnt = 1, .latched = 0 };
    int led_user = 0;             /* 按钮翻转的 LED 状态（正常温度档下生效） */
    TickType_t btn_show_until = 0;/* OLED 显示按钮提示的截止时间 */
    char btn_msg[32] = "";        /* OLED 按钮提示文字（UTF-8，"按钮12按下啦！"=20 字节+结束符） */

    for (;;)
    {
        int level = gpio_get_level(PIN_TEMP_DO);

        /* DO 去抖：连续 3 次采样一致才认定变化（仅用于日志） */
        if (level == last_level) { same_count++; }
        else { last_level = level; same_count = 1; }

        /* 按键1/2：去抖 + 锁存，一次按压只触发一次 */
        int pin_pressed = 0;
        if (btn_update(&btn1))      pin_pressed = PIN_BTN1;
        else if (btn_update(&btn2)) pin_pressed = PIN_BTN2;
        if (pin_pressed)
        {
            led_user = !led_user;
            gpio_set_level(PIN_LED, led_user);  /* 超温/低温档下会被报警逻辑覆盖，属正常 */
            btn_show_until = xTaskGetTickCount() + pdMS_TO_TICKS(BTN_PRESS_MS);
            snprintf(btn_msg, sizeof(btn_msg), "按钮%d按下啦！", pin_pressed);
            ESP_LOGI(TAG, "按钮%d按下 → LED=%d", pin_pressed, led_user);
        }

        /* 读 AO → 温度值 */
        float temp_c = temp_read_celsius();
        bool valid = !isnan(temp_c);

        /*
         * 三档判定：仅根据 AO 换算出的温度 temp_c。
         *   temp_c ≥ 35°C         → 超温
         *   25°C ≤ temp_c < 35°C  → 正常
         *   temp_c < 25°C         → 低温
         * 若 ADC 读数不可靠（NAN），保持上一次状态。
         */
        temp_state_t new_state = state; /* 默认保持不变 */
        if (valid)
        {
            if (temp_c >= 35.0f)        new_state = TEMP_STATE_OVER;
            else if (temp_c >= 25.0f)   new_state = TEMP_STATE_NORMAL;
            else                         new_state = TEMP_STATE_LOW;
        }

        /* 去抖后应用新状态 */
        if (same_count >= 3 && state != new_state)
        {
            state = new_state;
            const char *txt = (state == TEMP_STATE_OVER)  ? "超温"
                            : (state == TEMP_STATE_LOW)   ? "低温"
                            : "正常";
            ESP_LOGI(TAG, "状态变化 → %s (temp=%.1f°C)",
                     txt, (double)temp_c);
        }

        /* 每 1 秒刷新一次 OLED 显示（温度数值持续变化） */
        TickType_t now = xTaskGetTickCount();
        if ((now - last_refresh) >= pdMS_TO_TICKS(1000))
        {
            last_refresh = now;
            if (now < btn_show_until)
            {
                /* 按钮提示窗口内：显示是哪个按钮（顶部仍保留 SSID/地址） */
                oled_show_text(btn_msg);
            }
            else
            {
                const char *txt = (state == TEMP_STATE_OVER)  ? "超温"
                                : (state == TEMP_STATE_LOW)   ? "低温"
                                : "正常";
                oled_show_temp_status(txt, temp_c, valid);
            }
        }

        /* LED 模式：
         *   超温 → 常亮
         *   低温 → 闪烁（500ms 周期）
         *   正常 → 跟随按钮翻转的状态（led_user）
         */
        if (state == TEMP_STATE_OVER)
        {
            gpio_set_level(PIN_LED, 1);
        }
        else if (state == TEMP_STATE_LOW)
        {
            int blink = ((now - last_tick) / pdMS_TO_TICKS(250)) % 2;
            gpio_set_level(PIN_LED, blink);
        }
        else
        {
            gpio_set_level(PIN_LED, led_user);
            last_tick = now;  /* 重置闪烁计时 */
        }

        vTaskDelay(pdMS_TO_TICKS(250));  /* 4Hz 采样 */
    }
}
