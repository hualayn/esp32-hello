/**
 ******************************************************************************
 * @file    font6x8.h
 * @brief   6x8 ASCII font table (Jiangke OLED_F6x8 style) for the SSD1306.
 *
 *          95 printable ASCII chars (0x20..0x7E), 6 bytes each:
 *            - byte 0: 1-px left spacing (always 0x00)
 *            - bytes 1..5: glyph columns, one byte per column,
 *              bit 0 = top pixel, bits 0..7 = the 8 rows
 *
 *          Index for character c:  font6x8[(c - 0x20) * 6 + column]
 ******************************************************************************
 */

#ifndef FONT6X8_H
#define FONT6X8_H

#include <stdint.h>

/** 95 chars x 6 bytes per char (layout in the file header above). */
extern const uint8_t font6x8[95 * 6U];

#endif /* FONT6X8_H */
