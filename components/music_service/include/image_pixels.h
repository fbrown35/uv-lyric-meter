#pragma once
#include <stddef.h>
#include <stdint.h>
// P4 JPEG RGB565 is big-endian. LVGL's RGB565 buffer is native little-endian.
void image_rgb565_native(uint8_t *pixels,size_t bytes);
