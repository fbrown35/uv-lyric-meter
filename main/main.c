// M0 bring-up (landscape text + touch log) and M1 (microphone dBFS over serial).
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_in.h"
#include "music_ui.h"
#include "app_settings.h"
#include "music_service.h"
#include "music_net.h"
#include "vu_motion.h"
#include "board.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "music_display";

#if CONFIG_MUSIC_DISPLAY_DIAGNOSTICS
static lv_obj_t *s_coord_label;
static lv_obj_t *s_touch_dot;
static int64_t s_last_log_us;

#endif

static void log_system_info(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    ESP_LOGI(TAG, "Board: %s", CONFIG_MUSIC_DISPLAY_BOARD_NAME);
    ESP_LOGI(TAG, "ESP-IDF %s, %s rev v%u.%u, %u cores, flash %" PRIu32 " MB",
             esp_get_idf_version(), CONFIG_IDF_TARGET, chip.revision / 100, chip.revision % 100,
             (unsigned)chip.cores, flash_size >> 20);
    ESP_LOGI(TAG, "PSRAM %zu KB free / %zu KB, internal %zu KB free",
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM) >> 10,
             heap_caps_get_total_size(MALLOC_CAP_SPIRAM) >> 10,
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >> 10);
}

#if CONFIG_MUSIC_DISPLAY_DIAGNOSTICS
// Runs in the LVGL task (lock already held).
static void touch_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        lv_obj_clear_flag(s_touch_dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(s_touch_dot, p.x - 15, p.y - 15);
        lv_label_set_text_fmt(s_coord_label, "touch x=%d y=%d", (int)p.x, (int)p.y);

        int64_t now = esp_timer_get_time();
        if (code == LV_EVENT_PRESSED || now - s_last_log_us > 100000) {   // <=10 lines/s while dragging
            ESP_LOGI(TAG, "touch %s x=%d y=%d", code == LV_EVENT_PRESSED ? "down" : "move",
                     (int)p.x, (int)p.y);
            s_last_log_us = now;
        }
    } else if (code == LV_EVENT_RELEASED) {
        ESP_LOGI(TAG, "touch up   x=%d y=%d", (int)p.x, (int)p.y);
        lv_obj_add_flag(s_touch_dot, LV_OBJ_FLAG_HIDDEN);
    }
}

static lv_obj_t *corner_label(lv_obj_t *parent, const char *text, lv_align_t align)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFB000), 0);
    lv_obj_align(l, align, 0, 0);
    return l;
}

static void build_m0_screen(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);
    lv_obj_set_style_pad_all(scr, 12, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Music Display - M0");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -80);

    lv_obj_t *res = lv_label_create(scr);
    lv_label_set_text_fmt(res, "%d x %d landscape  |  touch anywhere", BOARD_UI_W, BOARD_UI_H);
    lv_obj_set_style_text_font(res, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(res, lv_color_hex(0xB0BEC5), 0);
    lv_obj_align(res, LV_ALIGN_CENTER, 0, -10);

    s_coord_label = lv_label_create(scr);
    lv_label_set_text(s_coord_label, "touch x=- y=-");
    lv_obj_set_style_text_font(s_coord_label, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(s_coord_label, lv_color_hex(0x4DD0E1), 0);
    lv_obj_align(s_coord_label, LV_ALIGN_CENTER, 0, 60);

    // Corner markers make a wrong rotation/mirror obvious at a glance.
    corner_label(scr, "TOP-LEFT", LV_ALIGN_TOP_LEFT);
    corner_label(scr, "TOP-RIGHT", LV_ALIGN_TOP_RIGHT);
    corner_label(scr, "BOTTOM-LEFT", LV_ALIGN_BOTTOM_LEFT);
    corner_label(scr, "BOTTOM-RIGHT", LV_ALIGN_BOTTOM_RIGHT);

    s_touch_dot = lv_obj_create(scr);
    lv_obj_set_size(s_touch_dot, 30, 30);
    lv_obj_set_style_radius(s_touch_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_touch_dot, lv_color_hex(0xFF5252), 0);
    lv_obj_set_style_border_width(s_touch_dot, 0, 0);
    lv_obj_add_flag(s_touch_dot, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
    lv_obj_clear_flag(s_touch_dot, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_event_cb(scr, touch_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, touch_event_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(scr, touch_event_cb, LV_EVENT_RELEASED, NULL);
}


#endif

#if CONFIG_MUSIC_DISPLAY_PCM_DUMP
static void dump_pcm_1s(int seq)
{
    size_t n = audio_in_sample_rate();
    int16_t *buf = heap_caps_malloc(n * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    uint64_t first = 0;
    if (!buf || !audio_in_copy_latest(buf, n, &first)) {
        free(buf);
        return;
    }
    printf("PCMBEGIN %d %u\n", seq, (unsigned)n);
    for (size_t i = 0; i < n; i += 64) {
        printf("PCM %d ", seq);
        for (size_t j = i; j < i + 64 && j < n; j++) {
            printf("%04x", (uint16_t)buf[j]);
        }
        printf("\n");
    }
    printf("PCMEND %d\n", seq);
    free(buf);
}
#endif

// M1: print the mic level 5x per second with a crude bar (-60..0 dBFS).
static void log_levels_forever(void)
{
    char bar[41];
    uint64_t last_blocks = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(200));
        audio_in_status_t st = audio_in_status();
        int n = (int)((st.level.rms_dbfs + 60.0f) * 40.0f / 60.0f);
        n = n < 0 ? 0 : (n > 40 ? 40 : n);
        memset(bar, '#', (size_t)n);
        memset(bar + n, '.', (size_t)(40 - n));
        bar[40] = '\0';
        ESP_LOGI(TAG, "mic rms %6.1f dBFS  peak %6.1f dBFS  [%s]  other slot %6.1f  blocks +%llu err %lu",
                 st.level.rms_dbfs, st.level.peak_dbfs, bar, st.other.rms_dbfs,
                 (unsigned long long)(st.block_count - last_blocks), (unsigned long)st.read_errors);
        last_blocks = st.block_count;
#if CONFIG_MUSIC_DISPLAY_PCM_DUMP
        static int ticks;
        if (++ticks % 50 == 0) {   // every 10 s
            dump_pcm_1s(ticks / 50);
        }
#endif
    }
}

void app_main(void)
{
    log_system_info();

    esp_err_t settings_err=app_settings_init();
    if(settings_err!=ESP_OK)ESP_LOGW(TAG,"Settings initialization: %s",esp_err_to_name(settings_err));
    app_settings_start_provisioning();
    music_service_start();
    music_net_start();

    board_display_t display;
    ESP_ERROR_CHECK(board_display_start(&display));

    if (display.disp) {
        lvgl_port_lock(0);
#if CONFIG_MUSIC_DISPLAY_DIAGNOSTICS
        build_m0_screen();
#else
        music_ui_start(display.disp);
#endif
        lvgl_port_unlock();
    }

    board_backlight_set(app_settings_get().brightness);
#if CONFIG_BOARD_DISPLAY_RAW_SCAN_TEST
    ESP_LOGI(TAG, "Raw framebuffer diagnostic ready; LVGL UI and touch disabled for this test.");
#elif CONFIG_MUSIC_DISPLAY_DIAGNOSTICS
    ESP_LOGI(TAG, "M0 ready: check text orientation and that the red dot follows your finger.");
#else
    ESP_LOGI(TAG, "M2 preview: 16 ms timer, mono microphone mirrored to L/R, 0 VU = %d dBFS.", CONFIG_MUSIC_DISPLAY_VU_REF_DBFS);
#endif

    i2c_master_bus_handle_t i2c_bus;
    ESP_ERROR_CHECK(board_i2c_init(&i2c_bus));
    esp_err_t err = audio_in_start(i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Audio input failed: %s", esp_err_to_name(err));
        return;
    }
    log_system_info();
    log_levels_forever();
}
