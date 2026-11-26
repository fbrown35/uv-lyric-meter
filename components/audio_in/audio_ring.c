#include "audio_ring.h"

#include <string.h>

void audio_ring_init(audio_ring_t *r, int16_t *storage, size_t capacity)
{
    r->buf = storage;
    r->capacity = capacity;
    r->total_written = 0;
}

void audio_ring_write(audio_ring_t *r, const int16_t *src, size_t n)
{
    if (n >= r->capacity) {
        // Only the tail fits; keep the counter consistent with what was "written".
        src += n - r->capacity;
        r->total_written += n - r->capacity;
        n = r->capacity;
    }
    size_t pos = (size_t)(r->total_written % r->capacity);
    size_t first = r->capacity - pos;
    if (first > n) {
        first = n;
    }
    memcpy(&r->buf[pos], src, first * sizeof(int16_t));
    if (n > first) {
        memcpy(r->buf, src + first, (n - first) * sizeof(int16_t));
    }
    r->total_written += n;
}

bool audio_ring_read_latest(const audio_ring_t *r, int16_t *dst, size_t n, uint64_t *first_index)
{
    if (n > r->capacity || r->total_written < n) {
        return false;
    }
    uint64_t start = r->total_written - n;
    size_t pos = (size_t)(start % r->capacity);
    size_t first = r->capacity - pos;
    if (first > n) {
        first = n;
    }
    memcpy(dst, &r->buf[pos], first * sizeof(int16_t));
    if (n > first) {
        memcpy(dst + first, r->buf, (n - first) * sizeof(int16_t));
    }
    if (first_index) {
        *first_index = start;
    }
    return true;
}
