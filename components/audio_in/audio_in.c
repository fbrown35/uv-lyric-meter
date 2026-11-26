#include "audio_in.h"

#include <string.h>

#include "audio_ring.h"
#include "driver/i2s_std.h"
#include "es8311_codec.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "audio_in";

#define SAMPLE_RATE   CONFIG_AUDIO_SAMPLE_RATE_HZ
#define BLOCK_SAMPLES (SAMPLE_RATE * AUDIO_BLOCK_MS / 1000)

#define AUDIO_TASK_CORE  1
#define AUDIO_TASK_PRIO  (configMAX_PRIORITIES - 2)
#define AUDIO_TASK_STACK 4096

static esp_codec_dev_handle_t s_mic;
static audio_ring_t s_ring;
static SemaphoreHandle_t s_ring_mutex;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static audio_in_status_t s_status = {
    .level = {LEVEL_FLOOR_DBFS, LEVEL_FLOOR_DBFS},
    .other = {LEVEL_FLOOR_DBFS, LEVEL_FLOOR_DBFS},
};

// Small, hot buffer stays in internal RAM (SPEC 14: realtime buffers internal, big ones PSRAM).
static int16_t s_stereo[BLOCK_SAMPLES * 2];
static int16_t s_block[BLOCK_SAMPLES];
static int16_t s_other[BLOCK_SAMPLES];

#ifdef CONFIG_AUDIO_MIC_SLOT_RIGHT
#define MIC_SLOT 1
#else
#define MIC_SLOT 0
#endif

static esp_err_t codec_init(i2c_master_bus_handle_t i2c_bus)
{
    i2s_chan_handle_t rx = NULL;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(CONFIG_AUDIO_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, NULL, &rx), TAG, "i2s channel");

    const i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = CONFIG_AUDIO_I2S_MCLK,
            .bclk = CONFIG_AUDIO_I2S_BCLK,
            .ws = CONFIG_AUDIO_I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = CONFIG_AUDIO_I2S_DIN,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(rx, &std_cfg), TAG, "i2s std");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(rx), TAG, "i2s enable");

    audio_codec_i2s_cfg_t i2s_if_cfg = {.port = CONFIG_AUDIO_I2S_PORT, .rx_handle = rx};
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_if_cfg);
    ESP_RETURN_ON_FALSE(data_if, ESP_FAIL, TAG, "i2s data if");

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = 0, // unused when bus_handle is given
        .addr = CONFIG_AUDIO_ES8311_ADDR,
        .bus_handle = i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    ESP_RETURN_ON_FALSE(ctrl_if, ESP_FAIL, TAG, "i2c ctrl if");

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    es8311_codec_cfg_t es_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_ADC,
        .pa_pin = -1,            // recording only: leave the speaker amplifier off
        .master_mode = false,    // P4 is I2S master and drives MCLK
        .use_mclk = true,
        .digital_mic = false,
    };
    const audio_codec_if_t *es8311 = es8311_codec_new(&es_cfg);
    ESP_RETURN_ON_FALSE(es8311, ESP_FAIL, TAG, "es8311 (I2C 0x%02x)", CONFIG_AUDIO_ES8311_ADDR);

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = es8311,
        .data_if = data_if,
    };
    s_mic = esp_codec_dev_new(&dev_cfg);
    ESP_RETURN_ON_FALSE(s_mic, ESP_FAIL, TAG, "codec dev");

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 2,
        .sample_rate = SAMPLE_RATE,
    };
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(s_mic, &fs) == ESP_CODEC_DEV_OK, ESP_FAIL, TAG, "codec open");
    ESP_RETURN_ON_FALSE(esp_codec_dev_set_in_gain(s_mic, (float)CONFIG_AUDIO_MIC_GAIN_DB) == ESP_CODEC_DEV_OK,
                        ESP_FAIL, TAG, "mic gain");
    return ESP_OK;
}

static void audio_task(void *arg)
{
    level_meter_t meter, meter_other;
    level_meter_init(&meter, SAMPLE_RATE);
    level_meter_init(&meter_other, SAMPLE_RATE);

    for (;;) {
        int ret = esp_codec_dev_read(s_mic, s_stereo, sizeof(s_stereo));
        if (ret != ESP_CODEC_DEV_OK) {
            taskENTER_CRITICAL(&s_status_lock);
            s_status.read_errors++;
            taskEXIT_CRITICAL(&s_status_lock);
            vTaskDelay(pdMS_TO_TICKS(AUDIO_BLOCK_MS));
            continue;
        }

        for (int i = 0; i < BLOCK_SAMPLES; i++) {
            s_block[i] = s_stereo[2 * i + MIC_SLOT];
            s_other[i] = s_stereo[2 * i + (1 - MIC_SLOT)];
        }
        level_result_t lvl = level_meter_process(&meter, s_block, BLOCK_SAMPLES);
        level_result_t lvl_other = level_meter_process(&meter_other, s_other, BLOCK_SAMPLES);

        if (xSemaphoreTake(s_ring_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            audio_ring_write(&s_ring, s_block, BLOCK_SAMPLES);
            xSemaphoreGive(s_ring_mutex);
        }

        taskENTER_CRITICAL(&s_status_lock);
        s_status.level = lvl;
        s_status.other = lvl_other;
        s_status.block_count++;
        taskEXIT_CRITICAL(&s_status_lock);
    }
}

esp_err_t audio_in_start(i2c_master_bus_handle_t i2c_bus)
{
    ESP_RETURN_ON_FALSE(i2c_bus, ESP_ERR_INVALID_ARG, TAG, "i2c bus");

    size_t ring_samples = (size_t)SAMPLE_RATE * CONFIG_AUDIO_RING_SECONDS;
    int16_t *storage = heap_caps_malloc(ring_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(storage, ESP_ERR_NO_MEM, TAG, "ring alloc (%u KB)",
                        (unsigned)(ring_samples * sizeof(int16_t) / 1024));
    audio_ring_init(&s_ring, storage, ring_samples);
    s_ring_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_ring_mutex, ESP_ERR_NO_MEM, TAG, "mutex");

    ESP_RETURN_ON_ERROR(codec_init(i2c_bus), TAG, "codec");

    BaseType_t ok = xTaskCreatePinnedToCore(audio_task, "audio_task", AUDIO_TASK_STACK, NULL,
                                            AUDIO_TASK_PRIO, NULL, AUDIO_TASK_CORE);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task");

    ESP_LOGI(TAG, "Capturing %d Hz stereo (mic = %s slot), %d ms blocks, gain %d dB, ring %d s (%u KB PSRAM)",
             SAMPLE_RATE, MIC_SLOT ? "right" : "left", AUDIO_BLOCK_MS, CONFIG_AUDIO_MIC_GAIN_DB, CONFIG_AUDIO_RING_SECONDS,
             (unsigned)(ring_samples * sizeof(int16_t) / 1024));
    return ESP_OK;
}

uint32_t audio_in_sample_rate(void)
{
    return SAMPLE_RATE;
}

audio_in_status_t audio_in_status(void)
{
    taskENTER_CRITICAL(&s_status_lock);
    audio_in_status_t st = s_status;
    taskEXIT_CRITICAL(&s_status_lock);
    return st;
}

bool audio_in_copy_latest(int16_t *dst, size_t n, uint64_t *first_index)
{
    if (!s_ring_mutex || xSemaphoreTake(s_ring_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }
    bool ok = audio_ring_read_latest(&s_ring, dst, n, first_index);
    xSemaphoreGive(s_ring_mutex);
    return ok;
}
