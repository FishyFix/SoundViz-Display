
#pragma once
#include <stdint.h>

struct Font {
    const uint8_t* data;  // flattened array
    uint8_t width;
    uint8_t height;
};

// Raw font data (extern)
extern const uint8_t font4x6[96][6];
extern const uint8_t font5x7[96][7];
extern const uint8_t font6x12[96][12];
extern const uint8_t font8x16[96][16];

// Font objects (extern)
extern const Font FONT_SMALL;
extern const Font FONT_MEDIUM;
extern const Font FONT_LARGE;
extern const Font FONT_XL;
