#include "recog_retry.h"
void recog_retry_begin(recog_retry_t *r, int64_t now) {
  *r = (recog_retry_t){
      .active = true, .due_ms = now, .ready_deadline_ms = now + 30000};
}
bool recog_retry_due(const recog_retry_t *r, int64_t now) {
  return r->active && now >= r->due_ms;
}
bool recog_retry_expired(const recog_retry_t *r, int64_t now) {
  return r->active && now >= r->ready_deadline_ms;
}
bool recog_retry_finish(recog_retry_t *r, int64_t now, bool success,
                        bool terminal) {
  r->attempts++;
  if (success || terminal || r->attempts >= 3) {
    r->active = false;
    return false;
  }
  r->due_ms = now + 8000;
  r->ready_deadline_ms = r->due_ms + 30000;
  return true;
}
