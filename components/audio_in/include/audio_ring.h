// Single-writer sample ring (SPEC 7.1: ~12 s in PSRAM, read back for song recognition).
// Pure C: the caller supplies storage and any locking. Unit-tested on the host.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int16_t *buf;
    size_t capacity;        // samples
    uint64_t total_written; // monotonically increasing sample counter
} audio_ring_t;

void audio_ring_init(audio_ring_t *r, int16_t *storage, size_t capacity);

void audio_ring_write(audio_ring_t *r, const int16_t *src, size_t n);

// Copies the most recent n samples (oldest first). Returns false if fewer than n have been
// written yet. *first_index receives the absolute index of dst[0] (for timestamping).
bool audio_ring_read_latest(const audio_ring_t *r, int16_t *dst, size_t n, uint64_t *first_index);

#ifdef __cplusplus
}
#endif
