#pragma once
#include <stddef.h>
#include <stdint.h>
// Remove clip DC and normalize only the recognition copy; never touches live meter/ADC gain.
float pcm_prepare_recognition(int16_t *samples, size_t count);
