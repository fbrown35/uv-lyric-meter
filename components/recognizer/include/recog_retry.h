#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct { bool active; unsigned attempts; int64_t due_ms, ready_deadline_ms; } recog_retry_t;
void recog_retry_begin(recog_retry_t *r,int64_t now_ms);
bool recog_retry_due(const recog_retry_t *r,int64_t now_ms);
bool recog_retry_expired(const recog_retry_t *r,int64_t now_ms);
// Count completed attempts. Failed matching retries twice with a fresh 8 s window.
bool recog_retry_finish(recog_retry_t *r,int64_t now_ms,bool success,bool terminal);
