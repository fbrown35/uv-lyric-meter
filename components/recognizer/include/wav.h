#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define WAV_HEADER_BYTES 44
// 48/44.1 kHz mono PCM -> 16 kHz PCM WAV. Windowed-sinc low pass prevents aliasing.
size_t wav_size(size_t input_samples, uint32_t input_rate);
bool wav_encode_16k(uint8_t *output, size_t capacity, const int16_t *input, size_t samples, uint32_t rate);

// Optional cooperative callback keeps an embedded CPU idle task schedulable.
bool wav_encode_16k_cooperative(uint8_t *output, size_t capacity, const int16_t *input, size_t samples, uint32_t rate, void (*yield_callback)(void));
