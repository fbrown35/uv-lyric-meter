#include "vinyl.h"
#include "esp_heap_caps.h"
#include <math.h>
#include <stdlib.h>

struct vinyl {
  lv_obj_t *root, *glint[2], *stamp;
  lv_point_precise_t points[2];
  void *pixels;
  int size, angle;
  int64_t last_update;
};

static void deleted(lv_event_t *event) {
  vinyl_t *record = lv_event_get_user_data(event);
  free(record->pixels);
  free(record);
}

static void disc(lv_layer_t *layer, int center, int radius, uint32_t color) {
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = lv_color_hex(color);
  d.radius = LV_RADIUS_CIRCLE;
  lv_area_t area = {center - radius, center - radius,
                    center + radius - 1, center + radius - 1};
  lv_draw_rect(layer, &d, &area);
}

vinyl_t *vinyl_create(lv_obj_t *parent, int size) {
  if (!parent || size < 64 || size > 512) return NULL;
  vinyl_t *record = calloc(1, sizeof(*record));
  if (!record) return NULL;
  size_t bytes = LV_CANVAS_BUF_SIZE(size, size, 16, LV_DRAW_BUF_STRIDE_ALIGN);
  record->pixels = heap_caps_aligned_alloc(
      LV_DRAW_BUF_ALIGN, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!record->pixels) { free(record); return NULL; }
  record->size = size;
  record->angle = -1;
  record->root = lv_obj_create(parent);
  lv_obj_remove_style_all(record->root);
  lv_obj_set_size(record->root, size, size);
  lv_obj_remove_flag(record->root, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(record->root, deleted, LV_EVENT_DELETE, record);
  lv_obj_t *canvas = lv_canvas_create(record->root);
  lv_canvas_set_buffer(canvas, record->pixels, size, size, LV_COLOR_FORMAT_RGB565);
  lv_obj_remove_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
  lv_canvas_fill_bg(canvas, lv_color_hex(0x14232F), LV_OPA_COVER);
  lv_layer_t layer;
  lv_canvas_init_layer(canvas, &layer);
  int center = size / 2, radius = size * 46 / 100;
  disc(&layer, center, radius, 0x48515A);
  disc(&layer, center, radius - 2, 0x090D12);
  for (int r = radius - 6; r > size * 18 / 100; r -= 5) {
    lv_draw_arc_dsc_t groove;
    lv_draw_arc_dsc_init(&groove);
    groove.center = (lv_point_t){center, center};
    groove.radius = r;
    groove.start_angle = 0;
    groove.end_angle = 360;
    groove.width = 1;
    groove.color = lv_color_hex(r % 2 ? 0x30363C : 0x242B32);
    lv_draw_arc(&layer, &groove);
  }
  disc(&layer, center, size * 17 / 100, 0xD9B472);
  disc(&layer, center, size * 15 / 100, 0xB47C45);
  disc(&layer, center, size * 12 / 100, 0xC79556);
  lv_canvas_finish_layer(canvas, &layer);

  for (int i = 0; i < 2; i++) {
    lv_obj_t *arc = record->glint[i] = lv_arc_create(record->root);
    lv_obj_remove_style_all(arc);
    int diameter = size * (i ? 66 : 84) / 100;
    lv_obj_set_size(arc, diameter, diameter);
    lv_obj_center(arc);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(0x8A9BA6), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, i ? 80 : 110, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, 2, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
  }
  record->stamp = lv_line_create(record->root);
  lv_obj_set_size(record->stamp, size, size);
  lv_obj_remove_flag(record->stamp, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_line_color(record->stamp, lv_color_hex(0xFFF0CF), 0);
  lv_obj_set_style_line_width(record->stamp, size >= 300 ? 4 : 3, 0);
  lv_obj_set_style_line_rounded(record->stamp, true, 0);
  lv_obj_t *hole = lv_obj_create(record->root);
  lv_obj_remove_style_all(hole);
  lv_obj_set_size(hole, size / 25, size / 25);
  lv_obj_set_style_radius(hole, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(hole, lv_color_hex(0x14232F), 0);
  lv_obj_set_style_bg_opa(hole, LV_OPA_COVER, 0);
  lv_obj_center(hole);
  lv_obj_remove_flag(hole, LV_OBJ_FLAG_CLICKABLE);
  vinyl_update(record, 0);
  return record;
}

void vinyl_update(vinyl_t *record, int64_t now_us) {
  if (!record || (record->angle >= 0 && now_us - record->last_update < 50000)) return;
  record->last_update = now_us;
  // Eight-second revolutions, independent of frame rate. Only small accents move.
  int angle = (int)((now_us % 8000000) * 360 / 8000000);
  if (angle == record->angle) return;
  record->angle = angle;
  lv_arc_set_angles(record->glint[0], angle, (angle + 32) % 360);
  lv_arc_set_angles(record->glint[1], (angle + 180) % 360, (angle + 215) % 360);
  float a = angle * 0.01745329252f;
  float center = record->size * .5f;
  for (int i = 0; i < 2; i++) {
    float radius = record->size * (i ? .115f : .065f);
    record->points[i] = (lv_point_precise_t){center + cosf(a) * radius,
                                            center + sinf(a) * radius};
  }
  lv_line_set_points(record->stamp, record->points, 2);
}
