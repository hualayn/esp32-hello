// u8g2_display.h —— OLED (SSD1306, I2C) 的 u8g2 显示接口
#pragma once

#include "esp_err.h"
#include "u8g2.h"

// 初始化 I2C 总线 + SSD1306（引脚 SDA=GPIO16, SCL=GPIO15，地址 0x3C）
esp_err_t oled_u8g2_init(void);

// 取得 u8g2 句柄，之后用 u8g2_ClearBuffer / u8g2_SetFont /
// u8g2_DrawStr / u8g2_DrawUTF8 / u8g2_SendBuffer 等 API 画画
u8g2_t *oled_u8g2_get(void);
