/**
  ******************************************************************************
  * @file    oled_i2c.c
  * @brief   0.96" SSD1306 OLED (128x64) over software I2C on PB8(SCL)/PB9(SDA).
  ******************************************************************************
  */
#include "oled_i2c.h"
#include "font6x8.h"
#include <string.h>

#define OLED_WIDTH  128U
#define OLED_HEIGHT 64U
#define OLED_PAGES  (OLED_HEIGHT / 8U)

/* ------------------------------------------------------------------ */
/* Soft I2C primitives: PB8 = SCL, PB9 = SDA                           */
/*                                                                     */
/* "High" is done by releasing the pin into input-with-internal-pull-  */
/* up mode (weak ~30-50k pull-up; most modules also have external      */
/* pull-ups). "Low" is an active open-drain drive.                     */
/* ------------------------------------------------------------------ */

static void delay_us(uint32_t us)
{
  volatile uint32_t i = us * 7U; /* ~7 cycles/us @ 72 MHz */
  while (i--)
  {
    __NOP();
  }
}

static void scl_high(void)
{
  GPIOB->CRH =
      (GPIOB->CRH & 0xFFFFFFF0UL) | 0x04UL; /* PB8 = CRH[3:0]: input + PU */
  delay_us(2);
}

static void scl_low(void)
{
  GPIOB->CRH =
      (GPIOB->CRH & 0xFFFFFFF0UL) | 0x05UL; /* PB8 = CRH[3:0]: OD out */
  GPIOB->ODR &= ~GPIO_PIN_8;
  delay_us(2);
}

static void sda_high(void)
{
  GPIOB->CRH = (GPIOB->CRH & 0xFFFFFF0FUL) |
               (0x04UL << 4); /* PB9 = CRH[7:4]: input + PU */
  delay_us(2);
}

static void sda_low(void)
{
  GPIOB->CRH =
      (GPIOB->CRH & 0xFFFFFF0FUL) | (0x05UL << 4); /* PB9 = CRH[7:4]: OD out */
  GPIOB->ODR &= ~GPIO_PIN_9;
  delay_us(2);
}

static inline uint8_t sda_read(void)
{
  return (GPIOB->IDR & GPIO_PIN_9) ? 1U : 0U;
}

static void i2c_start(void)
{
  sda_high();
  scl_high();
  sda_low();
  scl_low();
}

static void i2c_stop(void)
{
  sda_low();
  scl_high();
  sda_high();
}

static void i2c_send_byte(uint8_t b)
{
  for (uint8_t i = 0; i < 8U; i++)
  {
    if (b & 0x80U)
    {
      sda_high();
    }
    else
    {
      sda_low();
    }
    b <<= 1;
    scl_high();
    scl_low();
  }
}

/* @retval 0 = slave ACKed, 1 = NACK */
static uint8_t i2c_wait_ack(void)
{
  sda_high(); /* release SDA, slave drives it */
  scl_high();
  uint8_t nack = sda_read();
  scl_low();
  return nack;
}

/* ------------------------------------------------------------------ */
/* SSD1306 layer                                                       */
/* ------------------------------------------------------------------ */

static uint8_t fb[OLED_PAGES * OLED_WIDTH];
static uint8_t oled_addr;

/* Debug: first NACKed byte of the last OLED_Refresh:
 * 0xFFFF = address/control byte NACK, 1..1024 = data byte #0..#1023,
 * 0x0000 = every byte ACKed. */
volatile uint16_t oled_refresh_fail = 0xFFFFU;

/** @retval 1 if the address ACKed */
static uint8_t oled_probe(uint8_t addr)
{
  i2c_start();
  i2c_send_byte((uint16_t)addr << 1);
  uint8_t ack = (i2c_wait_ack() == 0U);
  i2c_stop();
  return ack;
}

int OLED_SendCmd(uint8_t c)
{
  if (oled_addr == 0U)
  {
    return -1; /* not initialized */
  }
  i2c_start();
  i2c_send_byte((uint16_t)oled_addr << 1);
  if (i2c_wait_ack())
  {
    i2c_stop();
    return 1;
  }
  i2c_send_byte(0x00U); /* control byte: command stream */
  if (i2c_wait_ack())
  {
    i2c_stop();
    return 2;
  }
  i2c_send_byte(c);
  uint8_t err = i2c_wait_ack();
  i2c_stop();
  return err;
}

static uint8_t oled_cmd(uint8_t c)
{
  return (uint8_t)OLED_SendCmd(c);
}

int OLED_Init(void)
{
  __HAL_RCC_GPIOB_CLK_ENABLE();
  delay_us(100); /* clock enable propagation */

  /* PB8/PB9 as open-drain outputs, released high */
  GPIOB->CRH = (GPIOB->CRH & 0xFFFF0000UL) | 0x05UL | (0x05UL << 8);
  GPIOB->ODR |= GPIO_PIN_8 | GPIO_PIN_9;
  scl_high();
  sda_high();
  delay_us(100);

  /* Probe common SSD1306 I2C addresses */
  if (oled_probe(0x78U))
  {
    oled_addr = 0x78U;
  }
  else if (oled_probe(0x3CU))
  {
    oled_addr = 0x3CU;
  }
  else
  {
    return -1; /* no display on the bus */
  }

  /* 128x64 init sequence */
  if (oled_cmd(0xAE) ||                   /* display off               */
      oled_cmd(0xD5) || oled_cmd(0x80) || /* clock divide      */
      oled_cmd(0xA8) || oled_cmd(0x3F) || /* multiplex 64      */
      oled_cmd(0xD3) || oled_cmd(0x00) || /* offset 0          */
      oled_cmd(0x40) ||                   /* start line 0      */
      oled_cmd(0x8D) || oled_cmd(0x14) || /* charge pump ON    */
      oled_cmd(0x20) || oled_cmd(0x00) || /* page addressing   */
      oled_cmd(0xA1) ||                   /* segment remap     */
      oled_cmd(0xC8) ||                   /* com scan reverse  */
      oled_cmd(0xDA) || oled_cmd(0x12) || /* com pins          */
      oled_cmd(0xD9) || oled_cmd(0xF1) || /* precharge         */
      oled_cmd(0x81) ||
      oled_cmd(
          0xFF) || /* contrast: max (some panels are invisible at default) */
      oled_cmd(0xA4) || /* RAM controls disp */
      oled_cmd(0xA6) || /* normal (not inv)  */
      oled_cmd(0xAF))   /* display ON        */
  {
    return -2; /* display NACKed during init */
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
  /* Column 0..127, pages 0..7 */
  if (oled_cmd(0x21) || oled_cmd(0x00) || oled_cmd(OLED_WIDTH - 1U) ||
      oled_cmd(0x22) || oled_cmd(0x00) || oled_cmd(OLED_PAGES - 1U))
  {
    oled_refresh_fail = 0xFFFFU;
    return;
  }

  i2c_start();
  i2c_send_byte((uint16_t)oled_addr << 1);
  if (i2c_wait_ack())
  {
    i2c_stop();
    oled_refresh_fail = 0xFFFEU;
    return;
  }
  i2c_send_byte(0x40U); /* control byte: data stream */
  if (i2c_wait_ack())
  {
    i2c_stop();
    oled_refresh_fail = 0xFFFDU;
    return;
  }

  for (size_t i = 0; i < sizeof(fb); i++)
  {
    i2c_send_byte(fb[i]);
    if (i2c_wait_ack())
    {
      i2c_stop();
      oled_refresh_fail = (uint16_t)(i + 1U);
      return;
    }
  }
  i2c_stop();
  oled_refresh_fail = 0U;
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
          fb[idx] |= bit; /* set pixel (light on dark bg) */
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
