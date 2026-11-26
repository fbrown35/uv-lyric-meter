// Board support for GUITION JC8012P4A1 (ESP32-P4, JD9365 MIPI-DSI 800x1280, GSL3680 touch).
// Only this component touches board GPIOs; everything else uses the handles it returns.
#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BOARD_LCD_NATIVE_W 800
#define BOARD_LCD_NATIVE_H 1280

// UI is designed for this logical (rotated) resolution.
#define BOARD_UI_W 1280
#define BOARD_UI_H 800

typedef struct {
    lv_display_t *disp;
    lv_indev_t *touch;   // NULL if touch init failed (display still usable)
} board_display_t;

// Shared I2C master bus (touch controller, audio codec). Initialized on first call.
esp_err_t board_i2c_init(i2c_master_bus_handle_t *out_bus);

// Starts LVGL port, MIPI-DSI panel, backlight and touch, rotated to 1280x800 landscape.
// LVGL objects may only be touched afterwards while holding lvgl_port_lock().
// In BOARD_DISPLAY_RAW_SCAN_TEST mode out->disp/touch remain NULL: no LVGL UI may be built.
esp_err_t board_display_start(board_display_t *out);

// 0..100 %
esp_err_t board_backlight_set(int percent);

#ifdef __cplusplus
}
#endif
