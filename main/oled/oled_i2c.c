/**
  ******************************************************************************
  * @file    oled_i2c.c
  * @brief   0.96" SSD1306 OLED (128x64) driver over ESP32 hardware I2C.
  *
  *          ESP32-S3 port of the STM32F103 bit-bang driver (see
  *          oled_i2c_stm32_ref.c for the original). Public API, framebuffer
  *          and font rendering are unchanged; only the I2C layer is
  *          replaced by the ESP-IDF hardware I2C master.
  *
  *          Wiring (4-pin module):
  *            VCC -> 3V3
  *            GND -> GND
  *            SCL -> GPIO15
  *            SDA -> GPIO16
  ******************************************************************************
  */
#include "oled_i2c.h"
#include "font6x8.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

#define OLED_WIDTH  128U
#define OLED_HEIGHT 64U
#define OLED_PAGES  (OLED_HEIGHT / 8U)

#define OLED_I2C_PORT   0
#define OLED_I2C_SDA    GPIO_NUM_16
#define OLED_I2C_SCL    GPIO_NUM_15
#define OLED_I2C_SPEED  100000U

static const char *TAG = "OLED";

static uint8_t fb[OLED_PAGES * OLED_WIDTH];
static i2c_master_bus_handle_t oled_bus;
static i2c_master_dev_handle_t oled_dev;

/* Debug: first NACKed byte of the last OLED_Refresh:
 * 0xFFFF = address/control byte NACK, 1..1024 = data byte #0..#1023,
 * 0x0000 = every byte ACKed. */
volatile uint16_t oled_refresh_fail = 0xFFFFU;

static esp_err_t oled_write(const uint8_t *data, size_t len)
{
    return i2c_master_transmit(oled_dev, data, len, 100);
}

int OLED_SendCmd(uint8_t c)
{
    if (oled_dev == NULL)
    {
        return -1; /* not initialized */
    }
    uint8_t buf[2] = { 0x00U, c }; /* control byte: command stream */
    return (oled_write(buf, 2) == ESP_OK) ? 0 : -1;
}

static int oled_cmd(uint8_t c)
{
    return OLED_SendCmd(c);
}

int OLED_Init(void)
{
    /* Hardware I2C bus: SDA=GPIO16, SCL=GPIO15, 100 kHz */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = OLED_I2C_PORT,
        .sda_io_num = OLED_I2C_SDA,
        .scl_io_num = OLED_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &oled_bus);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "I2C bus create failed: %s", esp_err_to_name(err));
        return -1;
    }

    /* Probe the common SSD1306 addresses: 0x3C (default) then 0x3D */
    static const uint8_t probe_addrs[2] = { 0x3CU, 0x3DU };

    for (size_t i = 0; i < sizeof(probe_addrs) && oled_dev == NULL; i++)
    {
        if (i2c_master_probe(oled_bus, probe_addrs[i], 50) != ESP_OK)
        {
            continue;
        }
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = probe_addrs[i],
            .scl_speed_hz = OLED_I2C_SPEED,
        };
        if (i2c_master_bus_add_device(oled_bus, &dev_cfg, &oled_dev) == ESP_OK)
        {
            ESP_LOGI(TAG, "SSD1306 found at 0x%02X", probe_addrs[i]);
        }
        else
        {
            oled_dev = NULL;
        }
    }

    if (oled_dev == NULL)
    {
        ESP_LOGE(TAG, "No SSD1306 answered at 0x3C/0x3D. "
               "Check wiring: VCC=3V3, GND=GND, SCL=GPIO15, SDA=GPIO16.");
        return -1;
    }

    /* 128x64 init sequence (identical to the STM32 version) */
    static const uint8_t init_seq[] = {
        0xAE,                   /* display off            */
        0xD5, 0x80,             /* clock divide           */
        0xA8, 0x3F,             /* multiplex 64           */
        0xD3, 0x00,             /* offset 0               */
        0x40,                   /* start line 0           */
        0x8D, 0x14,             /* charge pump ON         */
        0x20, 0x00,             /* page addressing        */
        0xA1,                   /* segment remap          */
        0xC8,                   /* com scan reverse       */
        0xDA, 0x12,             /* com pins               */
        0xD9, 0xF1,             /* precharge              */
        0x81, 0xFF,             /* contrast: max          */
        0xA4,                   /* RAM controls display   */
        0xA6,                   /* normal (not inverted)  */
        0xAF,                   /* display ON             */
    };
    for (size_t i = 0; i < sizeof(init_seq); i++)
    {
        if (oled_cmd(init_seq[i]) != 0)
        {
            ESP_LOGE(TAG, "OLED NACKed during init (cmd 0x%02X)", init_seq[i]);
            return -2;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return 0;
}

void OLED_Clear(void)
{
    memset(fb, 0, sizeof(fb));
}

void OLED_FillScreen(uint8_t color)
{
    memset(fb, color, sizeof(fb));
}

void OLED_Refresh(void)
{
    if (oled_dev == NULL)
    {
        oled_refresh_fail = 0xFFFFU;
        return;
    }

    /* Column range 0..127, page range 0..7 (same as STM32 version) */
    if (oled_cmd(0x21) || oled_cmd(0x00) || oled_cmd(OLED_WIDTH - 1U) ||
        oled_cmd(0x22) || oled_cmd(0x00) || oled_cmd(OLED_PAGES - 1U))
    {
        oled_refresh_fail = 0xFFFFU;
        return;
    }

    /* One transaction: control byte 0x40 (data stream) + full frame buffer */
    uint8_t header = 0x40U;
    uint8_t *buf = malloc(sizeof(fb) + 1U);
    if (buf == NULL)
    {
        oled_refresh_fail = 0xFFFFU;
        return;
    }
    buf[0] = header;
    memcpy(buf + 1, fb, sizeof(fb));

    esp_err_t err = i2c_master_transmit(oled_dev, buf, sizeof(fb) + 1U, 500);
    free(buf);

    if (err == ESP_OK)
    {
        oled_refresh_fail = 0U;
    }
    else
    {
        /* The new I2C API does not report which byte NACKed; flag it as
         * a full-frame failure. */
        oled_refresh_fail = 0xFFFEU;
        ESP_LOGW(TAG, "frame refresh failed: %s", esp_err_to_name(err));
    }
}


static void draw_char(int16_t x, uint8_t y, char c, uint8_t invert)
{
    if (c < 0x20 || c > 0x7E)
    {
        return;
    }
    const uint8_t *g = &font6x8[(uint16_t)(c - 0x20) * 6U];

    /* g[col] is one column (bit 0 = top row); 6 columns x 8 rows.
     * x may be negative or > 127: the off-screen part is clipped. */
    for (uint8_t col = 0; col < 6U; col++)
    {
        int16_t px = (int16_t)(x + col);
        if (px < 0 || px >= (int16_t)OLED_WIDTH)
        {
            continue;
        }
        for (uint8_t row = 0; row < 8U; row++)
        {
            if (g[col] & (1U << row))
            {
                int16_t yy = (int16_t)(y + row);
                if (yy < 0 || yy >= (int16_t)OLED_HEIGHT)
                {
                    continue;
                }
                uint16_t idx = (uint16_t)((yy / 8U) * OLED_WIDTH + px);
                uint8_t bit = (uint8_t)(1U << (yy % 8U));
                if (invert)
                {
                    fb[idx] &= ~bit; /* clear pixel (dark on light bg) */
                }
                else
                {
                    fb[idx] |= bit;  /* set pixel (light on dark bg)   */
                }
            }
        }
    }
}

void OLED_ShowChar(int16_t x, uint8_t y, char c)
{
    draw_char(x, y, c, 0U);
}

void OLED_ShowString(int16_t x, uint8_t y, const char *s)
{
    while (*s != '\0' && x < (int16_t)OLED_WIDTH)
    {
        draw_char(x, y, *s, 0U);
        x = (int16_t)(x + 6U);
        s++;
    }
}

/** Draw a string with inverted pixels (dark text on a light background). */
void OLED_ShowStringInv(int16_t x, uint8_t y, const char *s)
{
    while (*s != '\0' && x < (int16_t)OLED_WIDTH)
    {
        draw_char(x, y, *s, 1U);
        x = (int16_t)(x + 6U);
        s++;
    }
}

void OLED_DrawPixel(int16_t x, int16_t y)
{
    if (x < 0 || x >= (int16_t)OLED_WIDTH || y < 0 || y >= (int16_t)OLED_HEIGHT)
    {
        return;
    }
    uint16_t idx = (uint16_t)((y / 8U) * OLED_WIDTH + x);
    fb[idx] |= (uint8_t)(1U << (y % 8U));
}

void OLED_FillRect(int16_t x, int16_t y, int16_t w, int16_t h)
{
    for (int16_t j = 0; j < h; j++)
    {
        for (int16_t i = 0; i < w; i++)
        {
            OLED_DrawPixel(x + i, y + j);
        }
    }
}
