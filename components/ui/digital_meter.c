#include "digital_meter.h"
#include "ui_font.h"
#include <math.h>
#include <stdlib.h>
#define SEGMENTS 36
struct digital_meter {
  lv_obj_t *root, *segments[SEGMENTS], *value;
  uint32_t colors[SEGMENTS];
  int last_count, last_peak, last_tenth;
};
static void deleted(lv_event_t *e) { free(lv_event_get_user_data(e)); }
digital_meter_t *digital_meter_create(lv_obj_t *parent, int w, int h, int theme,
                                      const char *channel) {
  digital_meter_t *m = calloc(1, sizeof(*m));
  if (!m)
    return NULL;
  m->last_count = m->last_peak = m->last_tenth = -999;
  m->root = lv_obj_create(parent);
  lv_obj_remove_style_all(m->root);
  lv_obj_set_size(m->root, w, h);
  lv_obj_set_style_bg_color(m->root, lv_color_hex(0x101821), 0);
  lv_obj_set_style_bg_opa(m->root, LV_OPA_COVER, 0);
  lv_obj_clear_flag(m->root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(m->root, deleted, LV_EVENT_DELETE, m);
  lv_obj_t *label = lv_label_create(m->root);
  lv_label_set_text(label, channel);
  lv_obj_set_pos(label, 20, 16);
  lv_obj_set_style_text_font(label, ui_font(32), 0);
  lv_obj_set_style_text_color(label, lv_color_hex(0xCAD7E2), 0);
  int gap = 4, segment_w = (w - 40 - (SEGMENTS - 1) * gap) / SEGMENTS;
  for (int i = 0; i < SEGMENTS; i++) {
    float db = -60 + 63.0f * (i + 1) / SEGMENTS;
    m->colors[i] = theme == 1   ? 0x29DCE5
                   : theme == 2 ? 0xFFB949
                   : db < -6    ? 0x45D38A
                   : db < 0     ? 0xFFD05D
                                : 0xFF6255;
    m->segments[i] = lv_obj_create(m->root);
    lv_obj_remove_style_all(m->segments[i]);
    lv_obj_set_pos(m->segments[i], 20 + i * (segment_w + gap), 65);
    lv_obj_set_size(m->segments[i], segment_w, h - 125);
    lv_obj_set_style_radius(m->segments[i], 3, 0);
    lv_obj_set_style_bg_opa(m->segments[i], LV_OPA_COVER, 0);
    lv_obj_clear_flag(m->segments[i], LV_OBJ_FLAG_CLICKABLE);
  }
  m->value = lv_label_create(m->root);
  lv_obj_align(m->value, LV_ALIGN_BOTTOM_RIGHT, -20, -18);
  lv_obj_set_style_text_font(m->value, ui_font(24), 0);
  lv_obj_set_style_text_color(m->value, lv_color_hex(0xCAD7E2), 0);
  digital_meter_update(m, -60, -60);
  return m;
}
void digital_meter_update(digital_meter_t *m, float level, float peak) {
  if (!m || !isfinite(level) || !isfinite(peak))
    return;
  int count = (int)ceilf((level + 60) * SEGMENTS / 63),
      p = (int)ceilf((peak + 60) * SEGMENTS / 63) - 1;
  count = count < 0 ? 0 : count > SEGMENTS ? SEGMENTS : count;
  p = p < -1 ? -1 : p >= SEGMENTS ? SEGMENTS - 1 : p;
  if (count != m->last_count || p != m->last_peak) {
    for (int i = 0; i < SEGMENTS; i++) {
      uint32_t next = i == p ? 0xF3FAFF : i < count ? m->colors[i] : 0x23313C;
      uint32_t previous = i == m->last_peak   ? 0xF3FAFF
                          : i < m->last_count ? m->colors[i]
                                              : 0x23313C;
      if (m->last_count == -999 || next != previous)
        lv_obj_set_style_bg_color(m->segments[i], lv_color_hex(next), 0);
    }
    m->last_count = count;
    m->last_peak = p;
  }
  int tenth = (int)lroundf(level * 10);
  if (tenth != m->last_tenth) {
    lv_label_set_text_fmt(m->value, "%+.1f dB", level);
    m->last_tenth = tenth;
  }
}
lv_obj_t *digital_meter_object(digital_meter_t *m) {
  return m ? m->root : NULL;
}
