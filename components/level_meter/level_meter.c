#include "level_meter.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// RMS of a full-scale sine is 1/sqrt(2); AES17 adds this so that sine reads 0 dBFS.
#define AES17_OFFSET_DB 3.0103f

void level_meter_init(level_meter_t *m, uint32_t sample_rate_hz)
{
    // y[n] = x[n] - x[n-1] + R*y[n-1], corner ~= (1-R)*fs/(2*pi)
    m->hpf_r = expf(-2.0f * (float)M_PI * LEVEL_HPF_CORNER_HZ / (float)sample_rate_hz);
    m->hpf_x1 = 0.0f;
    m->hpf_y1 = 0.0f;
}

float level_amplitude_to_dbfs(float amplitude)
{
    if (amplitude <= 0.0f) {
        return LEVEL_FLOOR_DBFS;
    }
    float db = 20.0f * log10f(amplitude);
    return db < LEVEL_FLOOR_DBFS ? LEVEL_FLOOR_DBFS : db;
}

level_result_t level_meter_process(level_meter_t *m, const int16_t *samples, size_t n)
{
    level_result_t r = {LEVEL_FLOOR_DBFS, LEVEL_FLOOR_DBFS};
    if (n == 0) {
        return r;
    }

    const float scale = 1.0f / 32768.0f;
    float x1 = m->hpf_x1;
    float y1 = m->hpf_y1;
    const float R = m->hpf_r;
    double sum_sq = 0.0;
    float peak = 0.0f;

    for (size_t i = 0; i < n; i++) {
        float x = (float)samples[i] * scale;
        float y = x - x1 + R * y1;
        x1 = x;
        y1 = y;
        sum_sq += (double)y * (double)y;
        float a = fabsf(y);
        if (a > peak) {
            peak = a;
        }
    }
    m->hpf_x1 = x1;
    m->hpf_y1 = y1;

    float rms = (float)sqrt(sum_sq / (double)n);
    r.rms_dbfs = level_amplitude_to_dbfs(rms);
    if (r.rms_dbfs > LEVEL_FLOOR_DBFS) {
        r.rms_dbfs += AES17_OFFSET_DB;
    }
    r.peak_dbfs = level_amplitude_to_dbfs(peak);
    return r;
}
