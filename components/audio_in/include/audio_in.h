// Microphone capture (SPEC 7.1): ES8311 ADC over I2S -> 10 ms level blocks + PSRAM ring buffer.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "level_meter.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_BLOCK_MS 10

typedef struct {
    level_result_t level;   // latest 10 ms block of the microphone slot
    level_result_t other;   // same block, the other I2S slot (diagnostics)
    uint64_t block_count;   // blocks processed since start
    uint32_t read_errors;   // failed I2S/codec reads
} audio_in_status_t;

// Configures the codec on the shared I2C bus, starts I2S capture and the audio task
// (core 1, high priority, SPEC 12.1). Allocates the ring buffer in PSRAM.
esp_err_t audio_in_start(i2c_master_bus_handle_t i2c_bus);

uint32_t audio_in_sample_rate(void);

// Thread-safe snapshot of the latest level.
audio_in_status_t audio_in_status(void);

// Copies the most recent n mono samples from the ring (oldest first), e.g. the 8 s recognition clip.
// *first_index is the absolute sample index of dst[0]; divide by the sample rate for a timestamp.
bool audio_in_copy_latest(int16_t *dst, size_t n, uint64_t *first_index);

#ifdef __cplusplus
}
#endif
