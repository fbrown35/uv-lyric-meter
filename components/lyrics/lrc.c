#include "lrc.h"
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool timestamp(const char *s, const char **end, int32_t *ms) {
  if (*s++ != '[' || !isdigit((unsigned char)*s))
    return false;
  long minutes = 0;
  while (isdigit((unsigned char)*s)) {
    minutes = minutes * 10 + *s++ - '0';
    if (minutes > 10000)
      return false;
  }
  if (*s++ != ':' || !isdigit((unsigned char)s[0]) ||
      !isdigit((unsigned char)s[1]))
    return false;
  int seconds = (s[0] - '0') * 10 + s[1] - '0';
  s += 2;
  if (seconds >= 60)
    return false;
  int fraction = 0, place = 100;
  if (*s == '.' || *s == ':') {
    s++;
    if (!isdigit((unsigned char)*s))
      return false;
    while (isdigit((unsigned char)*s)) {
      if (place) {
        fraction += (*s - '0') * place;
        place /= 10;
      }
      s++;
    }
  }
  if (*s != ']')
    return false;
  *ms = (int32_t)(minutes * 60000 + seconds * 1000 + fraction);
  *end = s + 1;
  return true;
}
static bool store(lrc_document_t *d, const char *s, size_t n,
                  uint32_t *offset) {
  while (n && (s[n - 1] == '\r' || s[n - 1] == ' ' || s[n - 1] == '\t'))
    n--;
  if (n + 1 > LRC_TEXT_BYTES - d->used)
    return false;
  *offset = (uint32_t)d->used;
  memcpy(d->text + d->used, s, n);
  d->text[d->used + n] = 0;
  d->used += n + 1;
  return true;
}
bool lrc_parse(lrc_document_t *d, const char *source) {
  if (!d || !source)
    return false;
  memset(d, 0, sizeof(*d));
  const char *p = source;
  if (strlen(p) >= 3 && (unsigned char)p[0] == 0xef &&
      (unsigned char)p[1] == 0xbb && (unsigned char)p[2] == 0xbf)
    p += 3;
  const char *start = p;
  int offset = 0;
  while (*p) {
    const char *end = strchr(p, '\n');
    if (!end)
      end = p + strlen(p);
    if (end - p > 9 && strncmp(p, "[offset:", 8) == 0) {
      char *last;
      long value = strtol(p + 8, &last, 10);
      if (last < end && *last == ']' && value >= -600000 && value <= 600000)
        offset = (int)value;
    }
    p = *end ? end + 1 : end;
  }
  p = start;
  while (*p) {
    const char *end = strchr(p, '\n');
    if (!end)
      end = p + strlen(p);
    const char *text = p, *next = NULL;
    int32_t times[32], ms;
    size_t count = 0;
    while (text < end && timestamp(text, &next, &ms)) {
      if (count >= 32)
        goto fail;
      times[count++] = ms;
      text = next;
    }
    if (count) {
      uint32_t location;
      if (!store(d, text, (size_t)(end - text), &location))
        goto fail;
      for (size_t i = 0; i < count; i++) {
        if (d->count >= LRC_MAX_LINES)
          goto fail;
        d->lines[d->count++] = (lrc_line_t){times[i] - offset, location};
      }
    }
    p = *end ? end + 1 : end;
  }
  d->synced = d->count > 0;
  if (!d->synced) {
    p = start;
    while (*p) {
      const char *end = strchr(p, '\n');
      if (!end)
        end = p + strlen(p);
      if (end > p && *p != '[') {
        uint32_t location;
        if (d->count >= LRC_MAX_LINES ||
            !store(d, p, (size_t)(end - p), &location))
          goto fail;
        d->lines[d->count] = (lrc_line_t){(int32_t)(d->count * 5000), location};
        d->count++;
      }
      p = *end ? end + 1 : end;
    }
  }
  // Stable insertion sort preserves source order for equal timestamps.
  for (size_t i = 1; i < d->count; i++) {
    lrc_line_t line = d->lines[i];
    size_t j = i;
    while (j && d->lines[j - 1].time_ms > line.time_ms) {
      d->lines[j] = d->lines[j - 1];
      j--;
    }
    d->lines[j] = line;
  }
  return d->count > 0;
fail:
  memset(d, 0, sizeof(*d));
  return false;
}
int lrc_current_line(const lrc_document_t *d, int64_t ms) {
  if (!d || !d->count)
    return -1;
  size_t lo = 0, hi = d->count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (d->lines[mid].time_ms <= ms)
      lo = mid + 1;
    else
      hi = mid;
  }
  return (int)lo - 1;
}
const char *lrc_line_text(const lrc_document_t *d, int i) {
  return d && i >= 0 && (size_t)i < d->count ? d->text + d->lines[i].text_offset
                                             : "";
}

int32_t lrc_end_ms(const lrc_document_t *d, int32_t duration) {
  if (!d || !d->synced || !d->count)
    return duration > 0 ? duration : 0;
  int last = -1;
  for (size_t i = 0; i < d->count; i++) {
    const unsigned char *p = (const unsigned char *)lrc_line_text(d, (int)i);
    while (*p && isspace(*p))
      p++;
    if (*p)
      last = (int)i;
  }
  if (last < 0)
    return duration > 0 ? duration : 0;
  int32_t end = d->lines[last].time_ms + 5000;
  if ((size_t)last + 1 < d->count &&
      d->lines[d->count - 1].time_ms > d->lines[last].time_ms)
    end = d->lines[d->count - 1].time_ms;
  if (duration > 0 && duration < end)
    end = duration;
  return end > 0 ? end : 0;
}
