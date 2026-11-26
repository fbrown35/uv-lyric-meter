#include "image_pixels.h"
void image_rgb565_native(uint8_t *pixels, size_t bytes) {
  if (!pixels)
    return;
  for (size_t i = 0; i + 1 < bytes; i += 2) {
    uint8_t v = pixels[i];
    pixels[i] = pixels[i + 1];
    pixels[i + 1] = v;
  }
}
