#ifndef LOGO_HPP
#define LOGO_HPP

#include <Arduino.h>

// your bitmap (UNCHANGED DATA)
extern const uint8_t myBitmap1[] PROGMEM;

// Alias for compatibility
#define logo myBitmap1

// logo size
#define LOGO_WIDTH   80
#define LOGO_HEIGHT  80

#endif
