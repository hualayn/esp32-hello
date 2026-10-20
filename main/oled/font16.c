/**
 * @file    font16.c
 * @brief   16x16 CJK font table lookup (see font16.h).
 *
 *          The binary is embedded into the app image by EMBED_FILES
 *          (main/CMakeLists.txt); no filesystem needed.
 */
#include "font16.h"

#include <string.h>

/* Embedded by CMake EMBED_FILES (font16.bin). */
extern const uint8_t _binary_font16_bin_start[] asm("_binary_font16_bin_start");

static const uint8_t *s_tbl;   /* first entry (after 6-byte header)     */
static uint16_t s_count;       /* glyph count from the header           */
static int s_ready;            /* header parsed on first lookup         */

uint32_t font16_utf8_next(const char **p)
{
    const uint8_t *s = (const uint8_t *)*p;
    uint32_t cp;

    if (s[0] < 0x80U) {
        cp = s[0];
        *p += 1;
    } else if ((s[0] & 0xE0U) == 0xC0U && (s[1] & 0xC0U) == 0x80U) {
        cp = ((uint32_t)(s[0] & 0x1FU) << 6) | (uint32_t)(s[1] & 0x3FU);
        *p += 2;
    } else if ((s[0] & 0xF0U) == 0xE0U && (s[1] & 0xC0U) == 0x80U && (s[2] & 0xC0U) == 0x80U) {
        cp = ((uint32_t)(s[0] & 0x0FU) << 12) | ((uint32_t)(s[1] & 0x3FU) << 6) |
             (uint32_t)(s[2] & 0x3FU);
        *p += 3;
    } else {
        cp = 0xFFFDU; /* replacement character */
        *p += 1;
    }
    return cp;
}

const uint8_t *font16_lookup(uint32_t cp)
{
    if (s_ready == 0) {
        if (memcmp(_binary_font16_bin_start, "HZ16", 4) != 0) {
            return NULL;
        }
        s_count = (uint16_t)(_binary_font16_bin_start[4] |
                             (_binary_font16_bin_start[5] << 8));
        s_tbl = &_binary_font16_bin_start[6];
        s_ready = 1;
    }
    if (cp > 0xFFFFU) {
        return NULL;
    }

    uint16_t want = (uint16_t)cp;
    int lo = 0;
    int hi = (int)s_count - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        const uint8_t *e = &s_tbl[mid * 34];
        uint16_t c = (uint16_t)(e[0] | (e[1] << 8));
        if (c == want) {
            return e + 2;
        }
        if (c < want) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return NULL;
}
