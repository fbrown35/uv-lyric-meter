#include "pcm_prepare.h"
#include <math.h>
float pcm_prepare_recognition(int16_t *samples, size_t count) {
  if (!samples || !count)
    return 1;
  int64_t sum = 0;
  for (size_t i = 0; i < count; i++)
    sum += samples[i];
  int32_t mean = (int32_t)(sum / (int64_t)count), peak = 0;
  for (size_t i = 0; i < count; i++) {
    int32_t v = (int32_t)samples[i] - mean;
    if (v < 0)
      v = -v;
    if (v > peak)
      peak = v;
  }
  if (!peak) {
    for (size_t i = 0; i < count; i++)
      samples[i] = 0;
    return 1;
  }
  float gain = fminf(8.0f, 28000.0f / peak);
  for (size_t i = 0; i < count; i++) {
    float v = ((int32_t)samples[i] - mean) * gain;
    samples[i] = (int16_t)lroundf(fmaxf(-32768, fminf(32767, v)));
  }
  return gain;
}
