#include "wav.h"
#include <limits.h>
#include <math.h>
#include <string.h>
static void le16(uint8_t *p, uint16_t v) {
  p[0] = v;
  p[1] = v >> 8;
}
static void le32(uint8_t *p, uint32_t v) {
  le16(p, v);
  le16(p + 2, v >> 16);
}
size_t wav_size(size_t n, uint32_t rate) {
  if (rate < 16000 || rate > 96000 || n > UINT32_MAX / 2)
    return 0;
  uint64_t count = (uint64_t)n * 16000 / rate;
  return count > (UINT32_MAX - 44) / 2 ? 0 : 44 + (size_t)count * 2;
}
bool wav_encode_16k_cooperative(uint8_t *out, size_t cap, const int16_t *in,
                                size_t n, uint32_t rate,
                                void (*yield_callback)(void)) {
  size_t bytes = wav_size(n, rate);
  if (!out || !in || bytes < 44 || cap < bytes)
    return false;
  memcpy(out, "RIFF", 4);
  le32(out + 4, (uint32_t)bytes - 8);
  memcpy(out + 8, "WAVEfmt ", 8);
  le32(out + 16, 16);
  le16(out + 20, 1);
  le16(out + 22, 1);
  le32(out + 24, 16000);
  le32(out + 28, 32000);
  le16(out + 32, 2);
  le16(out + 34, 16);
  memcpy(out + 36, "data", 4);
  le32(out + 40, (uint32_t)bytes - 44);
  const double pi = 3.141592653589793, cutoff = 7000.0 / rate;
  float coefficients[49];
  bool integer_ratio = rate % 16000 == 0;
  if (integer_ratio) {
    for (int tap = -24; tap <= 24; tap++) {
      double sinc =
          tap == 0 ? 2 * cutoff : sin(2 * pi * cutoff * tap) / (pi * tap);
      coefficients[tap + 24] =
          (float)(sinc * (0.5 + 0.5 * cos(pi * tap / 25.0)));
    }
  }
  for (size_t i = 0; i < (bytes - 44) / 2; i++) {
    if (yield_callback && i % 512 == 0)
      yield_callback();
    if (integer_ratio) {
      long center = (long)(i * (rate / 16000));
      float sum_f = 0, weight_f = 0;
      for (int tap = -24; tap <= 24; tap++) {
        long index = center + tap;
        if (index < 0 || (size_t)index >= n)
          continue;
        float w = coefficients[tap + 24];
        sum_f += in[index] * w;
        weight_f += w;
      }
      float value = weight_f != 0 ? sum_f / weight_f : 0;
      value = fmaxf(-32768, fminf(32767, value));
      le16(out + 44 + i * 2, (uint16_t)(int16_t)lroundf(value));
      continue;
    }
    double center = (double)i * rate / 16000.0, sum = 0, weight = 0;
    long base = (long)floor(center);
    for (int tap = -24; tap <= 24; tap++) {
      long index = base + tap;
      if (index < 0 || (size_t)index >= n)
        continue;
      double w;
      if (integer_ratio)
        w = coefficients[tap + 24];
      else {
        double x = index - center,
               sinc = fabs(x) < 1e-10 ? 2 * cutoff
                                      : sin(2 * pi * cutoff * x) / (pi * x);
        w = sinc * (0.5 + 0.5 * cos(pi * x / 25.0));
      }
      sum += in[index] * w;
      weight += w;
    }
    double value = weight != 0 ? sum / weight : 0;
    value = fmax(-32768, fmin(32767, value));
    le16(out + 44 + i * 2, (uint16_t)(int16_t)lround(value));
  }
  return true;
}

bool wav_encode_16k(uint8_t *out, size_t cap, const int16_t *in, size_t n,
                    uint32_t rate) {
  return wav_encode_16k_cooperative(out, cap, in, n, rate, NULL);
}
