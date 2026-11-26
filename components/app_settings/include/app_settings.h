#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct {
    uint32_t version;
    int screen, style, analog_theme, digital_theme, vu_ref, lyric_delay_ms, interval_s, brightness;
    bool automatic, demo;
    char wifi_ssid[33], wifi_password[65], audd_token[256];
} app_config_t;
esp_err_t app_settings_init(void);
app_config_t app_settings_get(void);
esp_err_t app_settings_set(const app_config_t *config);
void app_settings_start_provisioning(void);

typedef bool (*app_control_handler_t)(const char *command);
void app_settings_register_control(app_control_handler_t handler);
