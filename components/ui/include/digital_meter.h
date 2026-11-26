#pragma once
#include "lvgl.h"
typedef struct digital_meter digital_meter_t;
digital_meter_t *digital_meter_create(lv_obj_t *parent, int width, int height, int theme, const char *channel);
lv_obj_t *digital_meter_object(digital_meter_t *meter);
void digital_meter_update(digital_meter_t *meter, float level_db, float peak_db);
