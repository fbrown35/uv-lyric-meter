#pragma once
#include "lvgl.h"
#include <stdint.h>

typedef struct vinyl vinyl_t;
// The parent owns the widget and its cached PSRAM drawing.
vinyl_t *vinyl_create(lv_obj_t *parent, int size);
void vinyl_update(vinyl_t *record, int64_t now_us);
