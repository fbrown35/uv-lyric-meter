#include "music_net.h"
#include "app_settings.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <string.h>
#include <time.h>
static atomic_bool connected;
static void event(void *arg, esp_event_base_t base, int32_t id, void *data) {
  (void)arg;
  (void)data;
  if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    atomic_store(&connected, true);
    ESP_LOGI("music_net", "Wi-Fi IP ready");
  }
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
    atomic_store(&connected, false);
}
bool music_net_connected(void) { return atomic_load(&connected); }
bool music_net_time_ready(void) { return time(NULL) > 1700000000; }
static void task(void *unused) {
  (void)unused;
  esp_err_t err = esp_netif_init();
  if (err != ESP_OK) {
    ESP_LOGE("music_net", "netif init: %s", esp_err_to_name(err));
    vTaskDelete(NULL);
    return;
  }
  err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    vTaskDelete(NULL);
    return;
  }
  esp_netif_create_default_wifi_sta();
  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, event, NULL);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, event, NULL);
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  err = esp_wifi_init(&init);
  if (err != ESP_OK) {
    ESP_LOGE("music_net", "Wi-Fi host init: %s", esp_err_to_name(err));
    vTaskDelete(NULL);
    return;
  }
  esp_wifi_set_storage(WIFI_STORAGE_RAM);
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_log_level_set("wifi", ESP_LOG_WARN);
  err = esp_wifi_start();
  if (err != ESP_OK) {
    ESP_LOGE("music_net", "Wi-Fi start: %s", esp_err_to_name(err));
    vTaskDelete(NULL);
    return;
  }
  esp_sntp_config_t ntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
  esp_netif_sntp_init(&ntp);
  app_config_t previous = {0};
  for (;;) {
    app_config_t c = app_settings_get();
    if (c.wifi_ssid[0]) {
      bool changed = strcmp(c.wifi_ssid, previous.wifi_ssid) != 0 ||
                     strcmp(c.wifi_password, previous.wifi_password) != 0;
      if (changed) {
        esp_wifi_disconnect();
        atomic_store(&connected, false);
        wifi_config_t wifi = {0};
        memcpy(wifi.sta.ssid, c.wifi_ssid, strlen(c.wifi_ssid));
        memcpy(wifi.sta.password, c.wifi_password, strlen(c.wifi_password));
        wifi.sta.pmf_cfg.capable = true;
        err = esp_wifi_set_config(WIFI_IF_STA, &wifi);
        memset(&wifi, 0, sizeof(wifi));
        if (err == ESP_OK)
          previous = c;
        else
          ESP_LOGE("music_net", "Wi-Fi configuration: %s",
                   esp_err_to_name(err));
      }
      if (!music_net_connected())
        esp_wifi_connect();
    }
    memset(&c, 0, sizeof(c));
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}
void music_net_start(void) {
  xTaskCreate(task, "net_task", 8192, NULL, 3, NULL);
}
