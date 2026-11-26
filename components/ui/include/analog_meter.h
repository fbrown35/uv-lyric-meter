// LVGL objects must be accessed under the LVGL port lock.
#pragma once
#include "lvgl.h"

typedef struct analog_meter analog_meter_t;
// The parent owns the widget; deleting it releases the PSRAM scale cache.
analog_meter_t *analog_meter_create(lv_obj_t *parent, int width, int height, const char *channel, int theme);
void analog_meter_update(analog_meter_t *meter, float amplitude, bool peak);

lv_obj_t *analog_meter_object(analog_meter_t *meter);
