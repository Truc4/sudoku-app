/* Built-in 5x7 bitmap font (ASCII 32..95, lower case folds to upper). Shared by every backend so text looks the
 * same everywhere and no font files are needed. Row r of glyph g is a 5-bit mask, bit 4 = leftmost pixel. */
#ifndef UI_FONT_GUARD
#define UI_FONT_GUARD
#include <stdint.h>
#define FONT_W 5
#define FONT_H 7
const uint8_t *func_glyph(int ch);   /* 7 row masks; unknown characters give '?' */
#endif
