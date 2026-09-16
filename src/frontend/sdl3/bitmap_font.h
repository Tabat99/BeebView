#ifndef VIEWBBC_BITMAP_FONT_H
#define VIEWBBC_BITMAP_FONT_H

#include <stdint.h>

/* Bedstead 3.261 SAA5050-style 20-pixel bitmap font. */
#define VIEWBBC_BITMAP_FONT_WIDTH 12
#define VIEWBBC_BITMAP_FONT_HEIGHT 20

const uint16_t *viewbbc_bitmap_font_glyph(uint8_t ch);

#endif
