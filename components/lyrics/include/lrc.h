#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define LRC_MAX_LINES 512
#define LRC_TEXT_BYTES 65536
// Allocate this large document in PSRAM on the device.
typedef struct { int32_t time_ms; uint32_t text_offset; } lrc_line_t;
typedef struct { bool synced; size_t count, used; lrc_line_t lines[LRC_MAX_LINES]; char text[LRC_TEXT_BYTES]; } lrc_document_t;
// Supports multiple timestamps, metadata and [offset:]. Positive LRC offset advances lyrics.
// Plain lyrics use one line per 5 seconds for slow scrolling; synced is false.
bool lrc_parse(lrc_document_t *doc, const char *source);
int lrc_current_line(const lrc_document_t *doc, int64_t position_ms);
const char *lrc_line_text(const lrc_document_t *doc, int index);
// Explicit trailing empty timestamp ends lyrics. Otherwise allow the last line 5 s,
// bounded by known track duration; unsynced lyrics use only known track duration.
int32_t lrc_end_ms(const lrc_document_t *doc,int32_t duration_ms);
