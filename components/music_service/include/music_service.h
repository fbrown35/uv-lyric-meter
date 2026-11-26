#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    bool connected, busy, has_track, synced, demo, lyrics_ended, request_pending;
    uint32_t track_id;
    int64_t recognized_us;
    char title[192], artist[192], album[192], release[32], status[128];
    int position_ms, duration_ms, line_index;
    char lines[7][512];
} music_snapshot_t;
typedef struct { uint8_t *pixels; int width, height, stride; uint32_t track_id; } music_art_t;
void music_service_start(void);
void music_service_recognize(void);
void music_service_snapshot(music_snapshot_t *out);
// Ownership of returned pixels passes to the UI; free when the image is no longer used.
bool music_service_take_art(music_art_t *out);
