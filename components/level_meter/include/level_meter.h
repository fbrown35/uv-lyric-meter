// Audio level measurement (SPEC 7.2). Pure C, no ESP-IDF dependencies: unit-tested on the host.
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Lowest level reported; digital silence maps here instead of -inf.
#define LEVEL_FLOOR_DBFS (-120.0f)

// DC-blocking high-pass corner (SPEC 7.2: ~20 Hz).
#define LEVEL_HPF_CORNER_HZ 20.0f

typedef struct {
    float hpf_r;     // pole of the 1st-order DC blocker
    float hpf_x1;    // previous input
    float hpf_y1;    // previous output
} level_meter_t;

typedef struct {
    // dBFS on the AES17 scale: a full-scale sine reads 0 dBFS (RMS + 3.01 dB).
    float rms_dbfs;
    // Sample peak of the filtered block, 0 dBFS = full-scale sample.
    float peak_dbfs;
} level_result_t;

void level_meter_init(level_meter_t *m, uint32_t sample_rate_hz);

// Filters one block (typically 10 ms) of 16-bit mono samples and returns its levels.
// Filter state carries over between calls.
level_result_t level_meter_process(level_meter_t *m, const int16_t *samples, size_t n);

// Helpers, exposed for tests and for the VU calibration (vu_ref_dbfs).
float level_amplitude_to_dbfs(float amplitude_0_to_1);

#ifdef __cplusplus
}
#endif
