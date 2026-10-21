// u8g2_display.c —— u8g2 ↔ ESP-IDF 硬件 I2C 适配层
//
// u8g2 是纯 C 库，不依赖硬件；它通过"字节回调"（byte callback）
// 向底层发数据。这里把回调接到 ESP-IDF 的 i2c_master 上，
// 和之前手写驱动一样的引脚：SDA=GPIO16, SCL=GPIO15，地址 0x3C。
#include "u8g2_display.h"

#include "driver/i2c_master.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"

#define OLED_I2C_PORT    I2C_NUM_0
#define OLED_I2C_SDA     GPIO_NUM_16
#define OLED_I2C_SCL     GPIO_NUM_15
#define OLED_I2C_ADDR    0x3C
#define OLED_I2C_FREQ_HZ 100000

static i2c_master_bus_handle_t i2c_bus;
static i2c_master_dev_handle_t oled_dev;
static u8g2_t                  u8g2;

// 一次 I2C 事务的字节缓存（fast_i2c 模式下每次最多 25 字节）
#define TX_BUF_SIZE 64
static uint8_t tx_buf[TX_BUF_SIZE];
static size_t  tx_len;

// u8x8 字节回调：u8g2 的所有数据最终都从这里走
// 消息含义：
//   U8X8_MSG_BYTE_INIT           —— 初始化（I2C 已在 oled_u8g2_init 建好）
//   U8X8_MSG_BYTE_START_TRANSFER —— 开始一次事务，开始攒字节
//   U8X8_MSG_BYTE_SEND           —— arg_int 个字节，在 arg_ptr 指向的位置
//   U8X8_MSG_BYTE_END_TRANSFER   —— 事务结束，把攒的字节一次性发出去
static uint8_t u8x8_byte_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    (void)u8x8;
    switch (msg) {
    case U8X8_MSG_BYTE_INIT:
        return 1;
    case U8X8_MSG_BYTE_START_TRANSFER:
        tx_len = 0;
        return 1;
    case U8X8_MSG_BYTE_SEND: {
        uint8_t *data = (uint8_t *)arg_ptr;
        while (arg_int-- > 0) {
            if (tx_len < TX_BUF_SIZE) {
                tx_buf[tx_len++] = *data++;
            }
        }
        return 1;
    }
    case U8X8_MSG_BYTE_END_TRANSFER:
        if (tx_len > 0) {
            if (i2c_master_transmit(oled_dev, tx_buf, tx_len, 100) != ESP_OK) {
                return 0;
            }
        }
        return 1;
    default:
        return 1; // SET_DC 等 SPI 专用消息，I2C 用不到
    }
}

// 复位/延时回调。
// 我们的 4 针 OLED（VCC/GND/SCL/SDA）没有把 RESET 脚引出来，
// 所以复位消息直接忽略；初始化时序要求的毫秒/微秒延时在这里实现。
static uint8_t u8x8_gpio_and_delay_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    (void)u8x8;
    (void)arg_ptr;
    switch (msg) {
    case U8X8_MSG_DELAY_MILLI:
        esp_rom_delay_us((uint32_t)arg_int * 1000);
        return 1;
    case U8X8_MSG_DELAY_10MICRO:
        esp_rom_delay_us((uint32_t)arg_int * 10);
        return 1;
    case U8X8_MSG_GPIO_RESET:
        /* 没有 RESET 脚，忽略 */
        return 1;
    default:
        return 1;
    }
}

esp_err_t oled_u8g2_init(void)
{
    // 1. 建 I2C 总线和设备（和手写驱动同样的配置）
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = OLED_I2C_PORT,
        .sda_io_num = OLED_I2C_SDA,
        .scl_io_num = OLED_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &i2c_bus);
    if (err != ESP_OK) {
        return err;
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = OLED_I2C_ADDR,
        .scl_speed_hz = OLED_I2C_FREQ_HZ,
    };
    err = i2c_master_bus_add_device(i2c_bus, &dev_cfg, &oled_dev);
    if (err != ESP_OK) {
        return err;
    }

    // 2. 配置 u8g2：SSD1306 128x64 + I2C，全屏缓冲（_f：1024 字节，一次发送整屏）
    //    注意：1 页缓冲（_1）一次只发 8 行，其余行显示的是芯片里的旧数据，
    //    多行布局必须用 _f 全屏缓冲
    //    U8G2_R0 = 屏幕方向 0 度（不旋转）
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&u8g2, U8G2_R0, u8x8_byte_cb, u8x8_gpio_and_delay_cb);

    // 3. 初始化显示芯片（u8g2 内部执行 SSD1306 初始化命令序列）
    u8g2_InitDisplay(&u8g2);

    // 4. 点亮屏幕（InitDisplay 后默认处于省电模式）
    u8g2_SetPowerSave(&u8g2, 0);

    return ESP_OK;
}

u8g2_t *oled_u8g2_get(void)
{
    return &u8g2;
}
