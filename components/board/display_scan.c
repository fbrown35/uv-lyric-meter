#include "display_scan.h"
#include <string.h>

bool display_scan_copy(uint16_t *dst, int width, int height, int offset,
                       int x1, int y1, int x2, int y2,
                       const uint16_t *src, size_t stride)
{
    if (!dst || !src || width <= 0 || height <= 0 || offset < 0 || offset >= height ||
        x1 < 0 || y1 < 0 || x2 < x1 || y2 < y1 || x2 >= width || y2 >= height ||
        stride < (size_t)(x2 - x1 + 1)) return false;
    for (int y = y1; y <= y2; y++) {
        int physical_y = (y + offset) % height;
        memcpy(dst + (size_t)physical_y * width + x1,
               src + (size_t)(y - y1) * stride, (size_t)(x2 - x1 + 1) * sizeof(*src));
    }
    return true;
}
