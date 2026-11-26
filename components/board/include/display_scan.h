// Pure-C RGB565 native-row remapping for a panel scan-origin calibration.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Copies an inclusive native rectangle from a strided source into a framebuffer.
// Positive offset moves source row 0 to framebuffer row offset, wrapping at height.
// Invalid rectangles/strides are rejected before any writes occur.
bool display_scan_copy(uint16_t *framebuffer, int width, int height, int offset,
                       int x1, int y1, int x2, int y2,
                       const uint16_t *source, size_t source_stride_pixels);
