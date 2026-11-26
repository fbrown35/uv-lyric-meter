#include "analog_meter.h"
#include "ui_font.h"
#include "esp_heap_caps.h"
#include <math.h>
#include <stdlib.h>

struct analog_meter {
    lv_obj_t *root, *needle, *peak;
    lv_point_precise_t points[2];
    void *pixels;
    int width, height;
    uint32_t needle_color;
};

static void deleted(lv_event_t *e)
{
    analog_meter_t *m = lv_event_get_user_data(e);
    free(m->pixels);
    free(m);
}

static lv_point_precise_t point(int w, int h, float amplitude, float radius)
{
    float angle = (210.0f + 120.0f * amplitude / 1.412538f) * 0.01745329252f;
    return (lv_point_precise_t){w * 0.5f + cosf(angle) * radius,
                                h * 0.87f + sinf(angle) * radius};
}

static void text(lv_layer_t *layer, int x, int y, int width, const char *value, uint32_t color, const lv_font_t *font)
{
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font = font;
    d.color = lv_color_hex(color);
    d.text = value;
    d.text_local = true;
    d.align = LV_TEXT_ALIGN_CENTER;
    lv_area_t area = {x, y, x + width - 1, y + 35};
    lv_draw_label(layer, &d, &area);
}

analog_meter_t *analog_meter_create(lv_obj_t *parent, int w, int h, const char *channel, int theme)
{
    if (!parent || w < 320 || h < 220 || w > 1280 || h > 800) return NULL;
    analog_meter_t *m = calloc(1, sizeof(*m));
    if (!m) return NULL;
    size_t size = LV_CANVAS_BUF_SIZE(w, h, 16, LV_DRAW_BUF_STRIDE_ALIGN);
    m->pixels = heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!m->pixels) { free(m); return NULL; }
    const uint32_t palettes[5][5] = {
        {0xE8D8B0, 0xFFF1D0, 0x302D28, 0xAF3025, 0xB92D25},
        {0xA86720, 0xFFCF7B, 0x352517, 0xA92C20, 0x8F281E},
        {0x10151D, 0x283342, 0xE4EAF0, 0xFF6255, 0xFFD279},
        {0x102E50, 0x3E79A9, 0xE1F4FF, 0xFF8B78, 0xFFDC99},
        {0x102C24, 0x3F7751, 0xD7FFD9, 0xFF9B61, 0xC2FF9E},
    };
    if (theme < 0 || theme >= 5) theme = 0;
    const uint32_t *palette = palettes[theme];
    m->needle_color = palette[4];
    m->width = w;
    m->height = h;
    m->root = lv_obj_create(parent);
    lv_obj_remove_style_all(m->root);
    lv_obj_set_size(m->root, w, h);
    lv_obj_clear_flag(m->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(m->root, deleted, LV_EVENT_DELETE, m);
    lv_obj_t *canvas = lv_canvas_create(m->root);
    lv_canvas_set_buffer(canvas, m->pixels, w, h, LV_COLOR_FORMAT_RGB565);
    lv_obj_clear_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_canvas_fill_bg(canvas, lv_color_hex(palette[0]), LV_OPA_COVER);
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    // A cached warm radial wash; only the needle is invalidated per frame.
    for (int radius = w; radius > 0; radius -= 10) {
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.radius = LV_RADIUS_CIRCLE;
        float f = (float)radius / w;
        d.bg_color = lv_color_mix(lv_color_hex(palette[0]), lv_color_hex(palette[1]), (lv_opa_t)(f * 255));
        lv_area_t a = {w / 2 - radius, h / 2 - radius, w / 2 + radius, h / 2 + radius};
        lv_draw_rect(&layer, &d, &a);
    }
    const int values[] = {-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3};
    const char *labels[] = {"-20", "-10", "-7", "-5", "-3", "-2", "-1", "0", "+1", "+2", "+3"};
    float radius = fminf(w * 0.46f, h * 0.76f);
    lv_draw_arc_dsc_t overload;
    lv_draw_arc_dsc_init(&overload);
    overload.center = (lv_point_t){w / 2, (int)(h * 0.87f)};
    overload.radius = radius - 5;
    overload.start_angle = 210 + 120 / 1.412538f;
    overload.end_angle = 330;
    overload.width = 5;
    overload.color = lv_color_hex(palette[3]);
    lv_draw_arc(&layer, &overload);
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        float amp = powf(10, values[i] / 20.0f);
        lv_draw_line_dsc_t line;
        lv_draw_line_dsc_init(&line);
        line.p1 = point(w, h, amp, radius);
        line.p2 = point(w, h, amp, radius - 18);
        line.width = values[i] == 0 ? 4 : 2;
        line.color = lv_color_hex(values[i] > 0 ? palette[3] : palette[2]);
        lv_draw_line(&layer, &line);
        lv_point_precise_t p = point(w, h, amp, radius + 22);
        text(&layer, p.x - 23, p.y - 12, 46, labels[i], values[i] > 0 ? palette[3] : palette[2], ui_font(14));
    }
    for (int percent = 0; percent <= 100; percent += 20) {
        float amplitude = percent / 100.0f;
        lv_draw_line_dsc_t tick;
        lv_draw_line_dsc_init(&tick);
        tick.color = lv_color_hex(palette[2]);
        tick.width = 1;
        tick.p1 = point(w, h, amplitude, radius - 38);
        tick.p2 = point(w, h, amplitude, radius - 45);
        lv_draw_line(&layer, &tick);
        lv_point_precise_t p = point(w, h, amplitude, radius - 60);
        char label[8];
        lv_snprintf(label, sizeof(label), "%d", percent);
        text(&layer, p.x - 18, p.y - 7, 36, label, palette[2], ui_font(14));
    }
    text(&layer, w / 2 - 70, h * 0.52f, 140, "VU", palette[2], ui_font(32));
    text(&layer, 18, h - 45, 80, channel ? channel : "", palette[2], ui_font(24));
    lv_canvas_finish_layer(canvas, &layer);
    m->needle = lv_line_create(m->root);
    lv_obj_set_style_line_width(m->needle, 3, 0);
    lv_obj_set_style_line_color(m->needle, lv_color_hex(m->needle_color), 0);
    lv_obj_set_style_line_rounded(m->needle, true, 0);
    m->peak = lv_obj_create(m->root);
    lv_obj_set_size(m->peak, 14, 14);
    lv_obj_align(m->peak, LV_ALIGN_TOP_RIGHT, -18, 18);
    lv_obj_set_style_radius(m->peak, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(m->peak, 0, 0);
    analog_meter_update(m, 0, false);
    return m;
}

void analog_meter_update(analog_meter_t *m, float amplitude, bool peak)
{
    if (!m) return;
    if (!isfinite(amplitude)) amplitude = 0;
    amplitude = fmaxf(0, fminf(amplitude, 1.412538f));
    m->points[0] = (lv_point_precise_t){m->width * 0.5f, m->height * 0.87f};
    m->points[1] = point(m->width, m->height, amplitude, fminf(m->width * 0.46f, m->height * 0.76f) - 10);
    lv_line_set_points(m->needle, m->points, 2);
    lv_obj_set_style_bg_color(m->peak, lv_color_hex(peak ? 0xFF3322 : 0x593D34), 0);
}

lv_obj_t *analog_meter_object(analog_meter_t *m)
{
    return m ? m->root : NULL;
}
