#include "app_settings.h"
#include "cJSON.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include <stdatomic.h>
#include <string.h>

static app_config_t config;
static SemaphoreHandle_t mutex;
static _Atomic(app_control_handler_t) control_handler;
void app_settings_register_control(app_control_handler_t handler) {
  atomic_store(&control_handler, handler);
}
static bool valid(const app_config_t *c) {
  return c && c->version == 1 && c->screen >= 0 && c->screen < 4 &&
         c->style >= 0 && c->style < 2 && c->analog_theme >= 0 &&
         c->analog_theme < 5 && c->digital_theme >= 0 && c->digital_theme < 3 &&
         c->vu_ref >= -60 && c->vu_ref <= 0 && c->lyric_delay_ms >= -10000 &&
         c->lyric_delay_ms <= 10000 && c->interval_s >= 30 &&
         c->interval_s <= 600 && c->brightness >= 5 && c->brightness <= 100 &&
         memchr(c->wifi_ssid, 0, sizeof(c->wifi_ssid)) &&
         memchr(c->wifi_password, 0, sizeof(c->wifi_password)) &&
         memchr(c->audd_token, 0, sizeof(c->audd_token)) &&
         !strpbrk(c->audd_token, "\r\n");
}
esp_err_t app_settings_init(void) {
  mutex = xSemaphoreCreateMutex();
  if (!mutex)
    return ESP_ERR_NO_MEM;
  config = (app_config_t){.version = 1,
                          .style = 1,
                          .vu_ref = CONFIG_MUSIC_DISPLAY_VU_REF_DBFS,
                          .lyric_delay_ms = 6400,
                          .interval_s = 90,
                          .brightness = 80,
                          .automatic = true};
  esp_err_t err = nvs_flash_init();
  if (err != ESP_OK)
    return err;
  nvs_handle_t nvs;
  err = nvs_open("music", NVS_READWRITE, &nvs);
  if (err != ESP_OK)
    return err;
  app_config_t stored;
  size_t bytes = sizeof(stored);
  if (nvs_get_blob(nvs, "config_v1", &stored, &bytes) == ESP_OK &&
      bytes == sizeof(stored) && valid(&stored))
    config = stored;
  nvs_close(nvs);
  return ESP_OK;
}
app_config_t app_settings_get(void) {
  xSemaphoreTake(mutex, portMAX_DELAY);
  app_config_t c = config;
  xSemaphoreGive(mutex);
  return c;
}
esp_err_t app_settings_set(const app_config_t *c) {
  if (!valid(c))
    return ESP_ERR_INVALID_ARG;
  xSemaphoreTake(mutex, portMAX_DELAY);
  nvs_handle_t nvs;
  esp_err_t err = nvs_open("music", NVS_READWRITE, &nvs);
  if (err == ESP_OK) {
    err = nvs_set_blob(nvs, "config_v1", c, sizeof(*c));
    if (err == ESP_OK)
      err = nvs_commit(nvs);
    nvs_close(nvs);
  }
  if (err == ESP_OK)
    config = *c;
  xSemaphoreGive(mutex);
  return err;
}
static bool string_field(cJSON *j, const char *key, char *out, size_t cap) {
  cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
  if (!cJSON_IsString(v) || strlen(v->valuestring) >= cap)
    return false;
  strcpy(out, v->valuestring);
  return true;
}
static bool public_options(cJSON *j, app_config_t *c) {
  if (!cJSON_IsObject(j) || !cJSON_GetArraySize(j))
    return false;
  struct {
    const char *name;
    int *value;
  } fields[] = {{"lyric_delay_ms", &c->lyric_delay_ms},
                {"vu_ref", &c->vu_ref},
                {"screen", &c->screen},
                {"style", &c->style},
                {"analog_theme", &c->analog_theme},
                {"digital_theme", &c->digital_theme},
                {"brightness", &c->brightness},
                {"interval_s", &c->interval_s}};
  cJSON *v;
  cJSON_ArrayForEach(v, j) {
    bool found = false;
    for (unsigned i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
      if (strcmp(v->string, fields[i].name) == 0 && cJSON_IsNumber(v) &&
          v->valuedouble == v->valueint) {
        *fields[i].value = v->valueint;
        found = true;
        break;
      }
    }
    if (!found && strcmp(v->string, "automatic") == 0 && cJSON_IsBool(v)) {
      c->automatic = cJSON_IsTrue(v);
      found = true;
    }
    if (!found && strcmp(v->string, "demo") == 0 && cJSON_IsBool(v)) {
      c->demo = cJSON_IsTrue(v);
      found = true;
    }
    if (!found)
      return false;
  }
  return true;
}
static void provision(void *unused) {
  (void)unused;
  char line[1600];
  size_t used = 0;
  bool overflow = false;
  for (;;) {
    char ch;
    int n = usb_serial_jtag_read_bytes(&ch, 1, pdMS_TO_TICKS(100));
    if (n <= 0)
      continue;
    if (ch == '\r')
      continue;
    if (ch != '\n') {
      if (used + 1 < sizeof(line))
        line[used++] = ch;
      else
        overflow = true;
      continue;
    }
    line[used] = 0;
    if (!overflow && strncmp(line, "PROVISION ", 10) == 0) {
      cJSON *j = cJSON_Parse(line + 10);
      app_config_t c = app_settings_get();
      bool ok =
          j && string_field(j, "wifi_ssid", c.wifi_ssid, sizeof(c.wifi_ssid)) &&
          string_field(j, "wifi_password", c.wifi_password,
                       sizeof(c.wifi_password)) &&
          string_field(j, "audd_token", c.audd_token, sizeof(c.audd_token));
      if (ok)
        ok = app_settings_set(&c) == ESP_OK;
      cJSON_Delete(j);
      memset(&c, 0, sizeof(c));
      // Never log received lines or credentials. The host waits for this fixed
      // acknowledgement.
      ESP_LOGI("provision", "%s", ok ? "PROVISION_OK" : "PROVISION_FAILED");
    }
    if (!overflow && strncmp(line, "SETTINGS ", 9) == 0) {
      cJSON *j = cJSON_Parse(line + 9);
      app_config_t c = app_settings_get();
      bool ok = public_options(j, &c) && app_settings_set(&c) == ESP_OK;
      cJSON_Delete(j);
      memset(&c, 0, sizeof(c));
      ESP_LOGI("settings", "%s", ok ? "SETTINGS_OK" : "SETTINGS_FAILED");
    }
    if (!overflow && strncmp(line, "CONTROL ", 8) == 0) {
      app_control_handler_t handler = atomic_load(&control_handler);
      bool accepted = handler && handler(line + 8);
      ESP_LOGI("control", "%s", accepted ? "CONTROL_OK" : "CONTROL_FAILED");
    }
    if (!overflow && strcmp(line, "GET_SETTINGS") == 0) {
      app_config_t c = app_settings_get();
      ESP_LOGI("settings",
               "SETTINGS_STATE screen=%d style=%d delay_ms=%d vu_ref=%d "
               "demo=%d automatic=%d",
               c.screen, c.style, c.lyric_delay_ms, c.vu_ref, c.demo,
               c.automatic);
      memset(&c, 0, sizeof(c));
    }
    memset(line, 0, sizeof(line));
    used = 0;
    overflow = false;
  }
}
void app_settings_start_provisioning(void) {
  if (!usb_serial_jtag_is_driver_installed()) {
    usb_serial_jtag_driver_config_t c = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    c.rx_buffer_size = 2048;
    esp_err_t err = usb_serial_jtag_driver_install(&c);
    if (err != ESP_OK) {
      ESP_LOGE("provision", "USB RX init: %s", esp_err_to_name(err));
      return;
    }
  }
  xTaskCreate(provision, "provision", 6144, NULL, 2, NULL);
}
