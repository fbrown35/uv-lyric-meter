#include "music_service.h"
#include "app_settings.h"
#include "audio_in.h"
#include "cJSON.h"
#include "driver/jpeg_decode.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "image_pixels.h"
#include "lrc.h"
#include "music_net.h"
#include "pcm_prepare.h"
#include "recog_retry.h"
#include "wav.h"
#include <ctype.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define JSON_CAP (256 * 1024)
static SemaphoreHandle_t lock;
static QueueHandle_t art_queue;
static music_snapshot_t state;
static lrc_document_t *current;
static int64_t anchor_us;
static int anchor_position_ms;
static int track_end_ms;
static bool art_ready;
static struct {
  char primary[768], fallback[768];
  uint32_t track_id;
  unsigned attempts;
  int64_t next_us;
} pending_art;
static atomic_bool manual;
static atomic_uint loop_ms, stage_ms;
static atomic_int active_stage;
static void stage(int value) {
  atomic_store(&active_stage, value);
  atomic_store(&stage_ms, (unsigned)(esp_timer_get_time() / 1000));
}
static const char *TAG = "music_service";
typedef struct {
  char title[192], artist[192];
  lrc_document_t *doc;
} cache_entry_t;
static cache_entry_t cache[20];
static unsigned cache_slot;

static void copy(char *out, size_t cap, const char *in) {
  if (!in)
    in = "";
  size_t n = strlen(in);
  if (n >= cap) {
    n = cap - 1;
    while (n && ((unsigned char)in[n] & 0xc0) == 0x80)
      n--;
  }
  memcpy(out, in, n);
  out[n] = 0;
}
static const char *string(cJSON *j, const char *key) {
  cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
  return cJSON_IsString(v) ? v->valuestring : "";
}
static void status(const char *value, bool busy) {
  xSemaphoreTake(lock, portMAX_DELAY);
  copy(state.status, sizeof(state.status), value);
  state.busy = busy;
  xSemaphoreGive(lock);
}
void music_service_recognize(void) {
  app_config_t cfg = app_settings_get();
  if (cfg.demo) {
    cfg.demo = false;
    app_settings_set(&cfg);
  }
  memset(&cfg, 0, sizeof(cfg));
  atomic_store(&manual, true);
  if (lock)
    status("다시 인식 요청을 받았어요", true);
  ESP_LOGI(TAG, "Manual recognition requested");
}
void music_service_snapshot(music_snapshot_t *out) {
  if (!lock) {
    memset(out, 0, sizeof(*out));
    copy(out->status, sizeof(out->status), "서비스 메모리 부족");
    return;
  }
  app_config_t c = app_settings_get();
  xSemaphoreTake(lock, portMAX_DELAY);
  *out = state;
  out->request_pending = atomic_load(&manual);
  out->connected = music_net_connected();
  int64_t ms = anchor_position_ms + (esp_timer_get_time() - anchor_us) / 1000 -
               c.lyric_delay_ms;
  if (ms < 0)
    ms = 0;
  if (ms > INT32_MAX)
    ms = INT32_MAX;
  out->position_ms = (int)ms;
  out->lyrics_ended = state.has_track && track_end_ms > 0 && ms >= track_end_ms;
  if (c.demo) {
    static const char *demo[] = {
        "조용한 방에 음악이 흐르면", "작은 바늘이 리듬을 그려요",
        "지금 이 순간을 천천히 따라", "화면에 한 줄의 빛을 남겨요",
        "이 문구는 가사 표시 시험용입니다"};
    out->has_track = true;
    out->demo = true;
    out->synced = true;
    out->duration_ms = 25000;
    out->position_ms = (int)((esp_timer_get_time() / 1000) % 25000);
    out->line_index = out->position_ms / 5000;
    copy(out->title, sizeof(out->title), "가사 표시 미리보기");
    copy(out->artist, sizeof(out->artist), "직접 작성한 시험 문구");
    copy(out->album, sizeof(out->album), "실제 인식 결과가 아닙니다");
    copy(out->status, sizeof(out->status), "미리보기 모드");
    for (int i = 0; i < 7; i++) {
      int index = out->line_index + i - 3;
      copy(out->lines[i], sizeof(out->lines[i]),
           index >= 0 && index < 5 ? demo[index] : "");
    }
  } else if (current) {
    out->synced = current->synced;
    out->line_index = lrc_current_line(
        current,
        current->synced ? ms : (esp_timer_get_time() - anchor_us) / 1000);
    for (int i = 0; i < 7; i++)
      copy(out->lines[i], sizeof(out->lines[i]),
           lrc_line_text(current, out->line_index + i - 3));
  } else {
    out->line_index = -1;
    for (int i = 0; i < 7; i++)
      out->lines[i][0] = 0;
  }
  xSemaphoreGive(lock);
}
bool music_service_take_art(music_art_t *out) {
  return art_queue && xQueueReceive(art_queue, out, 0) == pdTRUE;
}
typedef struct {
  uint8_t *data;
  size_t size, cap;
} response_t;
static esp_err_t http_event(esp_http_client_event_t *e) {
  response_t *r = e->user_data;
  if (e->event_id == HTTP_EVENT_ON_DATA) {
    if (e->data_len < 0 || (size_t)e->data_len >= r->cap - r->size)
      return ESP_ERR_NO_MEM;
    memcpy(r->data + r->size, e->data, e->data_len);
    r->size += e->data_len;
    r->data[r->size] = 0;
  }
  return ESP_OK;
}
static uint8_t *get(const char *url, size_t capacity, size_t *length,
                    int *http_status) {
  *length = 0;
  *http_status = 0;
  if (strncmp(url, "https://", 8) != 0)
    return NULL;
  response_t r = {.data = heap_caps_malloc(capacity + 1, MALLOC_CAP_SPIRAM),
                  .cap = capacity + 1};
  if (!r.data)
    return NULL;
  r.data[0] = 0;
  esp_http_client_config_t c = {.url = url,
                                .timeout_ms = 15000,
                                .crt_bundle_attach = esp_crt_bundle_attach,
                                .user_agent = "UVLyricMeter/0.2 (ESP32-P4)",
                                .event_handler = http_event,
                                .user_data = &r,
                                .buffer_size = 4096};
  esp_http_client_handle_t h = esp_http_client_init(&c);
  if (!h) {
    free(r.data);
    return NULL;
  }
  esp_err_t err = esp_http_client_perform(h);
  *http_status = esp_http_client_get_status_code(h);
  esp_http_client_cleanup(h);
  if (err != ESP_OK || *http_status != 200) {
    free(r.data);
    return NULL;
  }
  *length = r.size;
  return r.data;
}
static char *recognize(const app_config_t *cfg, const uint8_t *wav,
                       size_t bytes) {
  const char *boundary = "uv-lyric-meter-8fbe03721d";
  char prefix[768];
  int n = snprintf(
      prefix, sizeof(prefix),
      "--%s\r\nContent-Disposition: form-data; "
      "name=\"api_token\"\r\n\r\n%s\r\n--%s\r\nContent-Disposition: form-data; "
      "name=\"return\"\r\n\r\napple_music,spotify\r\n--%s\r\nContent-"
      "Disposition: form-data; name=\"file\"; "
      "filename=\"recording.wav\"\r\nContent-Type: audio/wav\r\n\r\n",
      boundary, cfg->audd_token, boundary, boundary);
  char tail[64];
  int tail_n = snprintf(tail, sizeof(tail), "\r\n--%s--\r\n", boundary);
  if (n < 0 || (size_t)n >= sizeof(prefix))
    return NULL;
  size_t upload_size = (size_t)n + bytes + (size_t)tail_n;
  uint8_t *upload = heap_caps_malloc(upload_size, MALLOC_CAP_SPIRAM);
  response_t response = {.data =
                             heap_caps_malloc(JSON_CAP + 1, MALLOC_CAP_SPIRAM),
                         .cap = JSON_CAP + 1};
  if (!upload || !response.data) {
    free(upload);
    free(response.data);
    memset(prefix, 0, sizeof(prefix));
    return NULL;
  }
  memcpy(upload, prefix, n);
  memcpy(upload + n, wav, bytes);
  memcpy(upload + n + bytes, tail, tail_n);
  memset(prefix, 0, sizeof(prefix));
  response.data[0] = 0;
  esp_http_client_config_t c = {.url = "https://api.audd.io/",
                                .method = HTTP_METHOD_POST,
                                .timeout_ms = 25000,
                                .crt_bundle_attach = esp_crt_bundle_attach,
                                .buffer_size = 4096,
                                .user_agent = "UVLyricMeter/0.2 (ESP32-P4)",
                                .event_handler = http_event,
                                .user_data = &response,
                                .disable_auto_redirect = true};
  esp_http_client_handle_t h = esp_http_client_init(&c);
  if (!h) {
    memset(upload, 0, upload_size);
    free(upload);
    free(response.data);
    return NULL;
  }
  char content_type[96];
  snprintf(content_type, sizeof(content_type),
           "multipart/form-data; boundary=%s", boundary);
  esp_http_client_set_header(h, "Content-Type", content_type);
  esp_http_client_set_post_field(h, (const char *)upload, (int)upload_size);
  ESP_LOGI(
      TAG, "Before recognition TLS: internal free=%u largest=%u",
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL |
                                                 MALLOC_CAP_8BIT));
  esp_err_t err = esp_http_client_perform(h);
  int http_status = esp_http_client_get_status_code(h);
  ESP_LOGI(TAG,
           "Recognition HTTP status: %d, response bytes: %u, transport: %s",
           http_status, (unsigned)response.size, esp_err_to_name(err));
  esp_http_client_cleanup(h);
  memset(upload, 0, upload_size);
  free(upload);
  if (err != ESP_OK || http_status != 200 || response.size == 0) {
    free(response.data);
    return NULL;
  }
  return (char *)response.data;
}

static void encode(char *out, size_t cap, const char *text) {
  static const char hex[] = "0123456789ABCDEF";
  size_t used = 0;
  for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
    if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~') {
      if (used + 1 >= cap)
        break;
      out[used++] = *p;
    } else {
      if (used + 3 >= cap)
        break;
      out[used++] = '%';
      out[used++] = hex[*p >> 4];
      out[used++] = hex[*p & 15];
    }
  }
  out[used] = 0;
}
static lrc_document_t *load_lyrics(const music_snapshot_t *track) {
  for (int i = 0; i < 20; i++)
    if (cache[i].doc && strcmp(cache[i].title, track->title) == 0 &&
        strcmp(cache[i].artist, track->artist) == 0)
      return cache[i].doc;
  char title[576], artist[576], album[576], url[2048];
  encode(title, sizeof(title), track->title);
  encode(artist, sizeof(artist), track->artist);
  encode(album, sizeof(album), track->album);
  if (track->duration_ms > 0)
    snprintf(url, sizeof(url),
             "https://lrclib.net/api/"
             "get?track_name=%s&artist_name=%s&album_name=%s&duration=%d",
             title, artist, album, track->duration_ms / 1000);
  else
    snprintf(
        url, sizeof(url),
        "https://lrclib.net/api/get?track_name=%s&artist_name=%s&album_name=%s",
        title, artist, album);
  size_t length;
  int code;
  uint8_t *body = get(url, JSON_CAP, &length, &code);
  ESP_LOGI(TAG, "Lyrics lookup HTTP status: %d, response bytes: %u", code,
           (unsigned)length);
  cJSON *json = body ? cJSON_Parse((char *)body) : NULL;
  free(body);
  if (!json && code == 404) {
    char query[1200];
    snprintf(query, sizeof(query), "%s%%20%s", artist, title);
    snprintf(url, sizeof(url), "https://lrclib.net/api/search?q=%s", query);
    body = get(url, JSON_CAP, &length, &code);
    ESP_LOGI(TAG, "Lyrics search HTTP status: %d, response bytes: %u", code,
             (unsigned)length);
    json = body ? cJSON_Parse((char *)body) : NULL;
    free(body);
    if (cJSON_IsArray(json)) {
      cJSON *best = NULL, *item;
      double distance = 1e9;
      cJSON_ArrayForEach(item, json) {
        ESP_LOGI(TAG, "Lyrics candidate: %s / %s", string(item, "trackName"),
                 string(item, "artistName"));
        if (strcasecmp(string(item, "trackName"), track->title) != 0 ||
            strcasecmp(string(item, "artistName"), track->artist) != 0)
          continue;
        cJSON *duration = cJSON_GetObjectItemCaseSensitive(item, "duration");
        double delta =
            cJSON_IsNumber(duration) && track->duration_ms > 0
                ? fabs(duration->valuedouble - track->duration_ms / 1000.0)
                : 0;
        if (delta < distance) {
          best = item;
          distance = delta;
        }
      }
      cJSON *selected =
          best && distance <= 3 ? cJSON_Duplicate(best, true) : NULL;
      cJSON_Delete(json);
      json = selected;
    }
  }
  lrc_document_t *doc = heap_caps_malloc(sizeof(*doc), MALLOC_CAP_SPIRAM);
  const char *text = string(json, "syncedLyrics");
  if (!*text)
    text = string(json, "plainLyrics");
  if (!doc || !*text || !lrc_parse(doc, text)) {
    free(doc);
    cJSON_Delete(json);
    return NULL;
  }
  cJSON_Delete(json);
  unsigned slot = cache_slot++ % 20;
  xSemaphoreTake(lock, portMAX_DELAY);
  if (current == cache[slot].doc)
    current = NULL;
  free(cache[slot].doc);
  cache[slot].doc = doc;
  copy(cache[slot].title, sizeof(cache[slot].title), track->title);
  copy(cache[slot].artist, sizeof(cache[slot].artist), track->artist);
  xSemaphoreGive(lock);
  return doc;
}
extern const uint8_t jpeg_test_start[] asm("_binary_jpeg_color_test_jpg_start");
extern const uint8_t jpeg_test_end[] asm("_binary_jpeg_color_test_jpg_end");
static bool decode_jpeg(const uint8_t *jpeg, size_t bytes, music_art_t *image) {
  jpeg_decode_picture_info_t info;
  jpeg_decoder_handle_t engine = NULL;
  if (jpeg_decoder_get_info(jpeg, bytes, &info) != ESP_OK || !info.width ||
      !info.height || info.width > 1024 || info.height > 1024)
    return false;
  image->width = info.width;
  image->height = info.height;
  image->stride = ((info.width + 15) & ~15) * 2;
  size_t size = (size_t)image->stride * ((info.height + 15) & ~15),
         allocated = 0;
  jpeg_decode_memory_alloc_cfg_t allocation = {
      .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER};
  image->pixels = jpeg_alloc_decoder_mem(size, &allocation, &allocated);
  if (!image->pixels)
    return false;
  jpeg_decode_engine_cfg_t ec = {.timeout_ms = 2000};
  esp_err_t err = jpeg_new_decoder_engine(&ec, &engine);
  if (err == ESP_OK) {
    jpeg_decode_cfg_t dc = {.output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
                            .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_RGB,
                            .conv_std = JPEG_YUV_RGB_CONV_STD_BT601};
    uint32_t written = 0;
    err = jpeg_decoder_process(engine, &dc, jpeg, bytes, image->pixels,
                               allocated, &written);
    jpeg_del_decoder_engine(engine);
  }
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "JPEG decode failed: %s", esp_err_to_name(err));
    free(image->pixels);
    image->pixels = NULL;
    return false;
  }
  image_rgb565_native(image->pixels,
                      (size_t)image->stride * ((image->height + 15) & ~15));
  return true;
}
static void jpeg_self_test(void) {
  music_art_t image = {0};
  if (!decode_jpeg(jpeg_test_start, jpeg_test_end - jpeg_test_start, &image)) {
    ESP_LOGE(TAG, "JPEG color self-test decode failed");
    return;
  }
  const uint16_t *pixels = (const uint16_t *)image.pixels;
  int stride = image.stride / 2;
  uint16_t r = pixels[16 * stride + 16], g = pixels[16 * stride + 48],
           b = pixels[48 * stride + 16];
  bool ok = (r & 0xf800) > 0xe000 && (r & 0x07e0) < 0x0100 && (r & 31) < 4 &&
            (g & 0x07e0) > 0x0600 && (b & 31) > 27 && (b & 0xf800) < 0x2000;
  ESP_LOGI(TAG, "JPEG RGB565 color self-test %s: R=%04x G=%04x B=%04x",
           ok ? "PASS" : "FAIL", r, g, b);
  free(image.pixels);
}
static bool art(const char *url, uint32_t track_id) {
  if (!*url)
    return false;
  size_t bytes;
  int code;
  uint8_t *jpeg = get(url, 1024 * 1024, &bytes, &code);
  if (!jpeg) {
    ESP_LOGW(TAG, "Album art download failed, HTTP %d", code);
    return false;
  }
  music_art_t image = {.track_id = track_id};
  bool ok = decode_jpeg(jpeg, bytes, &image);
  free(jpeg);
  if (!ok)
    return false;
  music_art_t old;
  if (xQueueReceive(art_queue, &old, 0) == pdTRUE)
    free(old.pixels);
  if (xQueueSend(art_queue, &image, 0) == pdTRUE) {
    ESP_LOGI(TAG, "Album art decoded: %d x %d stride=%d track=%u", image.width,
             image.height, image.stride, (unsigned)track_id);
    return true;
  }
  free(image.pixels);
  return false;
}
static void retry_artwork(void) {
  if (!pending_art.next_us || esp_timer_get_time() < pending_art.next_us)
    return;
  xSemaphoreTake(lock, portMAX_DELAY);
  bool stale = state.track_id != pending_art.track_id || art_ready;
  xSemaphoreGive(lock);
  if (stale) { pending_art.next_us = 0; return; }
  stage(5);
  pending_art.attempts++;
  ESP_LOGI(TAG, "Album art attempt %u, primary=%d fallback=%d",
           pending_art.attempts, pending_art.primary[0] != 0,
           pending_art.fallback[0] != 0);
  bool decoded = art(pending_art.primary, pending_art.track_id);
  if (!decoded && pending_art.fallback[0] &&
      strcmp(pending_art.primary, pending_art.fallback) != 0)
    decoded = art(pending_art.fallback, pending_art.track_id);
  xSemaphoreTake(lock, portMAX_DELAY);
  if (state.track_id == pending_art.track_id) art_ready = decoded;
  xSemaphoreGive(lock);
  pending_art.next_us = !decoded && pending_art.attempts < 3
                            ? esp_timer_get_time() + 5000000 : 0;
  if (pending_art.next_us)
    ESP_LOGW(TAG, "Album art retry queued without another recognition request");
}
static int timecode(const char *value) {
  int minutes;
  double seconds;
  char extra;
  if (sscanf(value, "%d:%lf%c", &minutes, &seconds, &extra) != 2 ||
      minutes < 0 || minutes > 1440 || seconds < 0 || seconds >= 60 ||
      !isfinite(seconds))
    return 0;
  return minutes * 60000 + (int)(seconds * 1000);
}
static void yield_cpu(void) { vTaskDelay(1); }
typedef enum {
  REQUEST_MATCHED,
  REQUEST_NO_MATCH,
  REQUEST_TEMP_ERROR,
  REQUEST_AUTH_ERROR,
  REQUEST_NOT_READY,
  REQUEST_OLD_TRACK
} request_result_t;
static request_result_t request(const app_config_t *cfg, bool waiting_next) {
  stage(1);
  uint32_t rate = audio_in_sample_rate();
  size_t samples = (size_t)rate * 8, bytes = wav_size(samples, rate);
  int16_t *pcm = heap_caps_malloc(samples * sizeof(*pcm), MALLOC_CAP_SPIRAM);
  uint8_t *wav = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  uint64_t index;
  int64_t start = esp_timer_get_time() - 8000000;
  if (!pcm || !wav || !audio_in_copy_latest(pcm, samples, &index)) {
    free(pcm);
    free(wav);
    status("녹음 버퍼를 준비하고 있어요", false);
    return REQUEST_NOT_READY;
  }
  status("곡을 인식하는 중…", true);
  float gain = pcm_prepare_recognition(pcm, samples);
  ESP_LOGI(TAG, "Recognition clip normalized, gain=%.2f", gain);
  stage(2);
  bool encoded =
      wav_encode_16k_cooperative(wav, bytes, pcm, samples, rate, yield_cpu);
  free(pcm);
  stage(3);
  char *body = encoded ? recognize(cfg, wav, bytes) : NULL;
  free(wav);
  bool received = body != NULL;
  cJSON *json = body ? cJSON_Parse(body) : NULL;
  free(body);
  if (!json) {
    status(received ? "인식 응답을 읽지 못했어요"
                    : "인식 서버 연결에 실패했어요",
           false);
    return REQUEST_TEMP_ERROR;
  }
  cJSON *error = cJSON_GetObjectItemCaseSensitive(json, "error");
  cJSON *error_code = cJSON_GetObjectItemCaseSensitive(error, "error_code");
  int code = cJSON_IsNumber(error_code) ? error_code->valueint : 0;
  if (strcmp(string(json, "status"), "error") == 0) {
    ESP_LOGW(TAG, "Recognition API error code: %d", code);
    cJSON_Delete(json);
    status(code == 900 || code == 901 ? "AudD 토큰을 확인해 주세요"
                                      : "곡 인식 서비스 오류",
           false);
    return code == 900 || code == 901 ? REQUEST_AUTH_ERROR : REQUEST_TEMP_ERROR;
  }
  cJSON *result = cJSON_GetObjectItemCaseSensitive(json, "result");
  if (strcmp(string(json, "status"), "success") != 0 ||
      !cJSON_IsObject(result) || !*string(result, "title")) {
    cJSON_Delete(json);
    status("곡을 찾지 못했어요", false);
    return REQUEST_NO_MATCH;
  }
  music_snapshot_t track = {.has_track = true};
  copy(track.title, sizeof(track.title), string(result, "title"));
  copy(track.artist, sizeof(track.artist), string(result, "artist"));
  copy(track.album, sizeof(track.album), string(result, "album"));
  copy(track.release, sizeof(track.release), string(result, "release_date"));
  cJSON *apple = cJSON_GetObjectItemCaseSensitive(result, "apple_music"),
        *spotify = cJSON_GetObjectItemCaseSensitive(result, "spotify"),
        *duration = cJSON_GetObjectItemCaseSensitive(apple, "durationInMillis");
  if (!cJSON_IsNumber(duration))
    duration = cJSON_GetObjectItemCaseSensitive(spotify, "duration_ms");
  if (cJSON_IsNumber(duration) && duration->valuedouble > 0 &&
      duration->valuedouble < 86400000)
    track.duration_ms = (int)duration->valuedouble;
  int position = timecode(string(result, "timecode"));
  char artwork[768] = {0};
  cJSON *aw = cJSON_GetObjectItemCaseSensitive(apple, "artwork");
  const char *template = string(aw, "url");
  if (*template) {
    size_t used = 0;
    while (*template && used + 5 < sizeof(artwork)) {
      if (strncmp(template, "{w}", 3) == 0 ||
          strncmp(template, "{h}", 3) == 0) {
        memcpy(artwork + used, "400", 3);
        used += 3;
        template += 3;
      } else
        artwork[used++] = *template++;
    }
    artwork[used] = 0;
  }
  char fallback_art[768] = {0};
  cJSON *album = cJSON_GetObjectItemCaseSensitive(spotify, "album"),
        *images = cJSON_GetObjectItemCaseSensitive(album, "images");
  cJSON *image =
      cJSON_GetArrayItem(images, cJSON_GetArraySize(images) > 1 ? 1 : 0);
  copy(fallback_art, sizeof(fallback_art), string(image, "url"));
  cJSON_Delete(json);
  xSemaphoreTake(lock, portMAX_DELAY);
  bool same = strcmp(state.title, track.title) == 0 &&
              strcmp(state.artist, track.artist) == 0;
  int candidate_position =
      position + (int)((esp_timer_get_time() - start) / 1000);
  if (waiting_next && same && candidate_position > 15000) {
    xSemaphoreGive(lock);
    status("이전 곡이 들려요 · 다음 곡을 기다리는 중…", false);
    return REQUEST_OLD_TRACK;
  }
  if (!same) {
    uint32_t id = state.track_id + 1;
    state = track;
    state.track_id = id;
    current = NULL;
    track_end_ms = 0;
    art_ready = false;
  }
  uint32_t track_id = state.track_id;
  bool need_art = !art_ready;
  anchor_position_ms = position;
  anchor_us = start;
  state.recognized_us = esp_timer_get_time();
  copy(state.status, sizeof(state.status), "가사를 찾는 중…");
  state.busy = true;
  xSemaphoreGive(lock);
  stage(4);
  lrc_document_t *doc = load_lyrics(&track);
  xSemaphoreTake(lock, portMAX_DELAY);
  if (doc || !same)
    current = doc;
  track_end_ms = lrc_end_ms(current, state.duration_ms);
  state.busy = false;
  copy(state.status, sizeof(state.status),
       doc ? (doc->synced ? "시간 가사" : "일반 가사 · 자동 스크롤")
           : "가사 정보 없음");
  xSemaphoreGive(lock);
  ESP_LOGI(TAG, "Recognition succeeded; lyrics=%s",
           doc ? (doc->synced ? "synced" : "plain") : "none");
  stage(5);
  if (need_art) {
    copy(pending_art.primary, sizeof(pending_art.primary), artwork);
    copy(pending_art.fallback, sizeof(pending_art.fallback), fallback_art);
    pending_art.track_id = track_id;
    pending_art.attempts = 0;
    pending_art.next_us = (*artwork || *fallback_art) ? esp_timer_get_time() : 0;
    if (!pending_art.next_us)
      ESP_LOGW(TAG, "Recognition metadata has no album art URL");
    retry_artwork();
  }
  ESP_LOGI(TAG, "Recognition worker stack minimum free: %u bytes",
           (unsigned)uxTaskGetStackHighWaterMark(NULL));
  return REQUEST_MATCHED;
}
static bool control(const char *command) {
  if (strcmp(command, "RECOGNIZE") == 0) {
    music_service_recognize();
    return true;
  }
  if (strcmp(command, "DIAG") != 0)
    return false;
  unsigned now = (unsigned)(esp_timer_get_time() / 1000);
  if (xSemaphoreTake(lock, pdMS_TO_TICKS(250)) != pdTRUE) {
    ESP_LOGE(TAG, "SERVICE_DIAG state mutex timeout");
    return true;
  }
  bool busy = state.busy, has_track = state.has_track;
  ESP_LOGI(TAG, "SERVICE_TRACK title=%s artist=%s duration=%d lyrics=%d art=%d status=%s",
           state.title, state.artist, state.duration_ms, current != NULL,
           art_ready, state.status);
  int end = track_end_ms;
  xSemaphoreGive(lock);
  ESP_LOGI(TAG,
           "SERVICE_DIAG stage=%d stage_age_ms=%u loop_age_ms=%u pending=%d "
           "wifi=%d time=%d busy=%d track=%d lyric_end=%d",
           atomic_load(&active_stage), now - atomic_load(&stage_ms),
           now - atomic_load(&loop_ms), atomic_load(&manual),
           music_net_connected(), music_net_time_ready(), busy, has_track, end);
  return true;
}
static void worker(void *unused) {
  (void)unused;
  jpeg_self_test();
  int64_t next = esp_timer_get_time() + 10000000, quiet_since = 0,
          handled_anchor = 0, next_track_try = 0;
  bool silent_armed = false, waiting_next = false;
  recog_retry_t retry = {0};
  for (;;) {
    atomic_store(&loop_ms, (unsigned)(esp_timer_get_time() / 1000));
    stage(0);
    app_config_t cfg = app_settings_get();
    int64_t now = esp_timer_get_time();
    float level = audio_in_status().level.rms_dbfs;
    if (atomic_exchange(&manual, false)) {
      recog_retry_begin(&retry, now / 1000);
      status("다시 인식 요청을 받았어요", true);
    }
    if (level < -55) {
      if (!quiet_since)
        quiet_since = now;
      if (now - quiet_since > 3000000)
        silent_armed = true;
    } else {
      quiet_since = 0;
      if (silent_armed) {
        next = now + 8000000;
        if (waiting_next)
          next_track_try = next;
        silent_armed = false;
      }
    }
    xSemaphoreTake(lock, portMAX_DELAY);
    int end_ms = track_end_ms;
    int64_t epoch = anchor_us;
    int64_t position =
        anchor_position_ms + (now - anchor_us) / 1000 - cfg.lyric_delay_ms;
    bool ended = state.has_track && end_ms > 0 && position >= end_ms;
    xSemaphoreGive(lock);
    if (cfg.automatic && !cfg.demo && ended && epoch != handled_anchor) {
      handled_anchor = epoch;
      waiting_next = true;
      next_track_try = now;
      status("가사가 끝났어요 · 다음 곡 인식 중…", false);
      ESP_LOGI(TAG,
               "Lyrics ended: scheduling immediate next-track recognition");
    }
    if (retry.active && recog_retry_expired(&retry, now / 1000)) {
      retry.active = false;
      status("음악 또는 연결 준비가 필요해요 · 다시 눌러 주세요", false);
    }
    bool manual_due = recog_retry_due(&retry, now / 1000);
    bool due =
        cfg.automatic && (waiting_next ? now >= next_track_try : now >= next);
    if (!cfg.demo && (manual_due || due)) {
      const char *blocked = NULL;
      if (!cfg.audd_token[0])
        blocked = "설정에서 AudD 토큰을 입력해 주세요";
      else if (!music_net_connected())
        blocked = "연결되면 인식을 다시 시도해요";
      else if (!music_net_time_ready())
        blocked = "보안 연결 시간을 맞추는 중…";
      else if (level < -55)
        blocked = "음악이 들리면 인식을 시작해요";
      if (blocked) {
        bool pending = retry.active && !recog_retry_expired(&retry, now / 1000);
        status(blocked, pending);
        if (!pending)
          retry.active = false;
      } else {
        ESP_LOGI(TAG, "Recognition attempt: %s, manual attempt %u",
                 waiting_next   ? "next-track"
                 : retry.active ? "manual"
                                : "periodic",
                 retry.attempts + 1);
        request_result_t result = request(&cfg, waiting_next);
        int64_t finished = esp_timer_get_time();
        next = finished + (int64_t)cfg.interval_s * 1000000;
        if (result == REQUEST_MATCHED) {
          waiting_next = false;
          recog_retry_finish(&retry, finished / 1000, true, false);
        } else if (result == REQUEST_NOT_READY) {
          status("녹음 버퍼를 준비하고 있어요", retry.active);
        } else {
          if (retry.active &&
              recog_retry_finish(&retry, finished / 1000, false,
                                 result == REQUEST_AUTH_ERROR)) {
            status("새 녹음으로 다시 듣는 중…", true);
            ESP_LOGI(TAG, "Manual retry queued with a fresh 8-second clip");
          }
          if (result == REQUEST_AUTH_ERROR)
            waiting_next = false;
          if (waiting_next)
            next_track_try =
                finished + (result == REQUEST_TEMP_ERROR ? 15000000 : 8000000);
        }
      }
    }
    if (!atomic_load(&manual) && music_net_connected() && music_net_time_ready())
      retry_artwork();
    memset(&cfg, 0, sizeof(cfg));
    vTaskDelay(pdMS_TO_TICKS(250));
  }
}
void music_service_start(void) {
  lock = xSemaphoreCreateMutex();
  art_queue = xQueueCreate(1, sizeof(music_art_t));
  if (!lock || !art_queue) {
    ESP_LOGE(TAG, "Unable to allocate service state");
    return;
  }
  app_settings_register_control(control);
  copy(state.status, sizeof(state.status), "음악을 듣는 중…");
  anchor_us = esp_timer_get_time();
  xTaskCreatePinnedToCore(worker, "recog_task", 24576, NULL, 2, NULL, 0);
}
