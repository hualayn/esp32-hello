/**
  ******************************************************************************
  * @file    oled_i2c.h
  * @brief   0.96" SSD1306 OLED (128x64) driver over ESP32 hardware I2C.
  *
  *          Wiring (4-pin module, ESP32-S3):
  *            VCC -> 3V3
  *            GND -> GND
  *            SCL -> GPIO15
  *            SDA -> GPIO16
  *
  *          ESP32-S3 port of the STM32F103 bit-bang driver; public API
  *          unchanged. Original: oled_i2c_stm32_ref.c.
  ******************************************************************************
  */
#ifndef OLED_I2C_H
#define OLED_I2C_H

#include <stdint.h>

/* Screen geometry (128x64 SSD1306). */
#define OLED_WIDTH  128U
#define OLED_HEIGHT 64U

/**
  * @brief Initialize I2C pins, probe the SSD1306 (0x78 then 0x3C) and
  *        run the display init sequence.
  * @retval 0 on success, -1 if no OLED ACKed its address.
  */
int OLED_Init(void);

/** Clear the internal frame buffer. */
void OLED_Clear(void);

/** Fill the frame buffer with a solid color (0 = black, 255 = white). */
void OLED_FillScreen(uint8_t color);

/** Push the frame buffer to the display (1024 bytes over I2C).
 *  The index of the first NACKed byte is stored in oled_refresh_fail
 *  (0xFFFF = address/control NACK, 0..1024 = data byte 0..1023,
 *  0x0000 = everything ACKed). */
void OLED_Refresh(void);

/** Send a single command byte to the display. @retval 0 = ACKed. */
int OLED_SendCmd(uint8_t c);

/** Debug: first NACKed byte during the last OLED_Refresh (see above). */
extern volatile uint16_t oled_refresh_fail;

/**
  * @brief Draw one 6x8 ASCII character (clipped at the screen edges).
  * @param x Start column, may be negative or > 127 (off-screen part is clipped).
  * @param y Start row 0..56 (pixel).
  * @param c Printable ASCII 0x20..0x7E.
  */
void OLED_ShowChar(int16_t x, uint8_t y, char c);

/** Draw a string starting at (x, y), 6 px advance per character. */
void OLED_ShowString(int16_t x, uint8_t y, const char *s);

/** Draw a string with inverted pixels (dark text on a light background). */
void OLED_ShowStringInv(int16_t x, uint8_t y, const char *s);

/**
 * @brief Draw one Unicode code point with the 16x16 CJK font.
 * @return advance width in px (16 = glyph drawn, 8 = not in the font table).
 */
int16_t OLED_ShowChar16(int16_t x, int16_t y, uint32_t cp);

/**
 * @brief Draw a UTF-8 string with the 16x16 CJK font (CJK + ASCII).
 *        Wraps to the next 16px row when the screen edge is reached.
 * @return the y of the last used row.
 */
int16_t OLED_ShowString16(int16_t x, int16_t y, const char *utf8);

/**
 * @brief Set one frame-buffer pixel (off-screen coordinates are clipped).
 * @param x Column 0..127.
 * @param y Row 0..63 (pixel).
 */
void OLED_DrawPixel(int16_t x, int16_t y);

/**
 * @brief Fill a w x h rectangle in the frame buffer (clipped at edges).
 */
void OLED_FillRect(int16_t x, int16_t y, int16_t w, int16_t h);

#endif /* OLED_I2C_H */
