#include "music_ui.h"
#include "analog_meter.h"
#include "app_settings.h"
#include "audio_in.h"
#include "board.h"
#include "digital_meter.h"
#include "digital_motion.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "music_service.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "ui_font.h"
#include "vu_motion.h"
#include "vinyl.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static lv_obj_t *root, *status_label, *title_label, *artist_label, *album_label,
    *lyrics_labels[7], *art_image;
static lv_obj_t *credential_fields[3], *keyboard;
static lv_obj_t *recognize_button, *art_tile, *hit_area, *chrome[32];
static unsigned chrome_count;
static bool idle_hidden, fading_out, wake_gesture;
static int64_t last_activity, fade_deadline, ignore_actions_until;
static void chrome_opacity(void *object, int32_t value) {
  lv_obj_set_style_opa(object, value, 0);
}
static void animate_chrome(lv_obj_t *object, int end, int duration) {
  lv_anim_delete(object, chrome_opacity);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, object);
  lv_anim_set_values(&a, lv_obj_get_style_opa(object, LV_PART_MAIN), end);
  lv_anim_set_duration(&a, duration);
  lv_anim_set_exec_cb(&a, chrome_opacity);
  lv_anim_start(&a);
}
static void wake_chrome(void) {
  if (!idle_hidden)
    return;
  idle_hidden = false;
  fading_out = false;
  wake_gesture = true;
  ignore_actions_until = esp_timer_get_time() + 150000;
  for (unsigned i = 0; i < chrome_count; i++) {
    lv_obj_clear_flag(chrome[i], LV_OBJ_FLAG_HIDDEN);
    animate_chrome(chrome[i], 255, 200);
  }
  ESP_LOGI("music_ui", "Auxiliary controls restored");
}
static void activity(lv_event_t *event) {
  lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING ||
      code == LV_EVENT_RELEASED) {
    last_activity = esp_timer_get_time();
    if (code == LV_EVENT_PRESSED)
      wake_chrome();
  }
}
static analog_meter_t *analog[2];
static digital_meter_t *digital[2];
static vu_motion_t needle;
static digital_motion_t leds;
static app_config_t cfg;
static bool rebuild;
static int settings_page;
static int64_t last_frame, report_time, last_text;
static unsigned rendered, updates;
static lv_point_t press_point;
static int previous_line = -999;
static music_art_t artwork;
static uint32_t displayed_track_id;
static lv_image_dsc_t artwork_descriptor;
static vinyl_t *default_record;
static bool audio_quiet;
static int64_t quiet_since;
static int64_t last_recognized_us;
static const char *analog_names[] = {"클래식", "호박색", "블랙", "블루",
                                     "그린"};
static const char *digital_names[] = {"신호등", "청록", "호박색"};
static const char *screen_names[] = {"전체", "미터", "곡 정보", "가사 집중"};
static const char *TAG = "music_ui";

static lv_obj_t *label(lv_obj_t *parent, const char *text, int size, int x,
                       int y, int width) {
  lv_obj_t *o = lv_label_create(parent);
  lv_label_set_text(o, text);
  lv_obj_set_pos(o, x, y);
  if (width > 0)
    lv_obj_set_width(o, width);
  lv_obj_set_style_text_font(o, ui_font(size), 0);
  lv_obj_set_style_text_color(o, lv_color_hex(0xDEE8F2), 0);
  return o;
}
static void set_text(lv_obj_t *o, const char *text) {
  if (o && strcmp(lv_label_get_text(o), text) != 0)
    lv_label_set_text(o, text);
}
static void save(void) {
  app_config_t next = app_settings_get();
  next.screen = cfg.screen;
  next.style = cfg.style;
  next.analog_theme = cfg.analog_theme;
  next.digital_theme = cfg.digital_theme;
  next.vu_ref = cfg.vu_ref;
  next.lyric_delay_ms = cfg.lyric_delay_ms;
  next.interval_s = cfg.interval_s;
  next.brightness = cfg.brightness;
  next.automatic = cfg.automatic;
  next.demo = cfg.demo;
  esp_err_t err = app_settings_set(&next);
  if (err != ESP_OK)
    ESP_LOGE(TAG, "Settings save: %s", esp_err_to_name(err));
}
static void action(lv_event_t *e) {
  last_activity = esp_timer_get_time();
  int id = (int)(intptr_t)lv_event_get_user_data(e);
  switch (id) {
  case 1:
    cfg.style = !cfg.style;
    break;
  case 2:
    if (cfg.style)
      cfg.digital_theme = (cfg.digital_theme + 1) % 3;
    else
      cfg.analog_theme = (cfg.analog_theme + 1) % 5;
    break;
  case 3:
    settings_page = 1;
    break;
  case 4:
    settings_page = 0;
    break;
  case 5:
    ESP_LOGI(TAG, "Recognition button click accepted");
    music_service_recognize();
    return;
  case 6:
    cfg.vu_ref = cfg.vu_ref > -60 ? cfg.vu_ref - 1 : -60;
    break;
  case 7:
    cfg.vu_ref = cfg.vu_ref < 0 ? cfg.vu_ref + 1 : 0;
    break;
  case 8:
    cfg.lyric_delay_ms =
        cfg.lyric_delay_ms > -10000 ? cfg.lyric_delay_ms - 100 : -10000;
    break;
  case 9:
    cfg.lyric_delay_ms =
        cfg.lyric_delay_ms < 10000 ? cfg.lyric_delay_ms + 100 : 10000;
    break;
  case 10:
    cfg.interval_s = cfg.interval_s > 30 ? cfg.interval_s - 30 : 30;
    break;
  case 11:
    cfg.interval_s = cfg.interval_s < 600 ? cfg.interval_s + 30 : 600;
    break;
  case 12:
    cfg.automatic = !cfg.automatic;
    break;
  case 13:
    cfg.brightness = cfg.brightness > 15 ? cfg.brightness - 10 : 5;
    board_backlight_set(cfg.brightness);
    break;
  case 14:
    cfg.brightness = cfg.brightness < 90 ? cfg.brightness + 10 : 100;
    board_backlight_set(cfg.brightness);
    break;
  case 15:
    cfg.demo = !cfg.demo;
    break;
  case 16:
    settings_page = 2;
    break;
  case 17: {
    app_config_t next = app_settings_get();
    const char *ssid = lv_textarea_get_text(credential_fields[0]),
               *password = lv_textarea_get_text(credential_fields[1]),
               *token = lv_textarea_get_text(credential_fields[2]);
    if (strlen(ssid) >= sizeof(next.wifi_ssid) ||
        strlen(password) >= sizeof(next.wifi_password) ||
        strlen(token) >= sizeof(next.audd_token))
      return;
    strcpy(next.wifi_ssid, ssid);
    strcpy(next.wifi_password, password);
    strcpy(next.audd_token, token);
    if (app_settings_set(&next) != ESP_OK)
      return;
    memset(&next, 0, sizeof(next));
    settings_page = 1;
    break;
  }
  default:
    if (id >= 100 && id < 104) {
      cfg.screen = id - 100;
      settings_page = 0;
    } else
      return;
  }
  save();
  rebuild = true;
}
static lv_obj_t *button(const char *text, int x, int y, int w, int id) {
  lv_obj_t *o = lv_button_create(root);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_size(o, w, 52);
  lv_obj_set_style_bg_color(o, lv_color_hex(0x243B4E), 0);
  lv_obj_set_style_radius(o, 12, 0);
  lv_obj_t *l = label(o, text, 24, 0, 0, 0);
  lv_obj_center(l);
  lv_obj_add_event_cb(o, action, LV_EVENT_CLICKED, (void *)(intptr_t)id);
  return o;
}
static void touch(lv_event_t *e) {
  lv_indev_t *indev = lv_indev_active();
  if (!indev)
    return;
  lv_point_t p;
  lv_indev_get_point(indev, &p);
  lv_event_code_t code = lv_event_get_code(e);
  if (wake_gesture || esp_timer_get_time() < ignore_actions_until)
    return;
  bool meter_area = cfg.screen == 1 || (cfg.screen == 0 && p.y < 460);
  if (code == LV_EVENT_PRESSED)
    press_point = p;
  else if (code == LV_EVENT_DOUBLE_CLICKED && meter_area) {
    cfg.style = !cfg.style;
    save();
    rebuild = true;
  } else if (code == LV_EVENT_LONG_PRESSED) {
    music_service_recognize();
  } else if (code == LV_EVENT_RELEASED) {
    int dx = p.x - press_point.x, dy = p.y - press_point.y;
    if (abs(dx) > 80 && abs(dx) > abs(dy)) {
      cfg.screen = (cfg.screen + (dx < 0 ? 1 : 3)) % 4;
      save();
      rebuild = true;
    } else if (meter_area && abs(dy) > 70 && abs(dy) > abs(dx)) {
      if (cfg.style)
        cfg.digital_theme = (cfg.digital_theme + (dy < 0 ? 1 : 2)) % 3;
      else
        cfg.analog_theme = (cfg.analog_theme + (dy < 0 ? 1 : 4)) % 5;
      save();
      rebuild = true;
    }
  }
}
static void focused(lv_event_t *e) {
  lv_keyboard_set_textarea(keyboard, lv_event_get_target_obj(e));
}
static void credentials(void) {
  label(root, "Wi-Fi · AudD 설정", 32, 32, 32, 800);
  button("돌아가기", 1060, 24, 180, 4);
  app_config_t c = app_settings_get();
  const char *names[] = {"Wi-Fi 이름", "Wi-Fi 비밀번호", "AudD API 토큰"};
  const char *values[] = {c.wifi_ssid, c.wifi_password, c.audd_token};
  for (int i = 0; i < 3; i++) {
    label(root, names[i], 24, 36, 110 + i * 100, 240);
    credential_fields[i] = lv_textarea_create(root);
    lv_obj_set_pos(credential_fields[i], 300, 100 + i * 100);
    lv_obj_set_size(credential_fields[i], 920, 74);
    lv_obj_set_style_text_font(credential_fields[i], ui_font(24), 0);
    lv_textarea_set_one_line(credential_fields[i], true);
    lv_textarea_set_max_length(credential_fields[i], i == 0   ? 32
                                                     : i == 1 ? 64
                                                              : 255);
    if (i)
      lv_textarea_set_password_mode(credential_fields[i], true);
    lv_textarea_set_text(credential_fields[i], values[i]);
    lv_obj_add_event_cb(credential_fields[i], focused, LV_EVENT_FOCUSED, NULL);
  }
  memset(&c, 0, sizeof(c));
  button("저장하고 연결", 900, 414, 320, 17);
  keyboard = lv_keyboard_create(root);
  lv_obj_set_size(keyboard, 1280, 300);
  lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(keyboard, credential_fields[0]);
}
static void settings(void) {
  char value[128];
  label(root, "설정", 32, 32, 32, 700);
  button("닫기", 1060, 24, 180, 4);
  snprintf(value, sizeof(value), "감도: %d dBFS = 0 VU", cfg.vu_ref);
  label(root, value, 24, 40, 132, 480);
  button("더 민감하게", 550, 116, 240, 6);
  button("덜 민감하게", 820, 116, 240, 7);
  snprintf(value, sizeof(value), "가사 지연: %+.1f초",
           cfg.lyric_delay_ms / 1000.0);
  label(root, value, 24, 40, 224, 480);
  button("-0.1초", 550, 208, 240, 8);
  button("+0.1초", 820, 208, 240, 9);
  snprintf(value, sizeof(value), "자동 인식 간격: %d초", cfg.interval_s);
  label(root, value, 24, 40, 316, 480);
  button("-30초", 550, 300, 240, 10);
  button("+30초", 820, 300, 240, 11);
  snprintf(value, sizeof(value), "밝기: %d%%", cfg.brightness);
  label(root, value, 24, 40, 408, 480);
  button("어둡게", 550, 392, 240, 13);
  button("밝게", 820, 392, 240, 14);
  button(cfg.automatic ? "자동 인식: 켜짐" : "자동 인식: 꺼짐", 40, 506, 340,
         12);
  button(cfg.demo ? "미리보기: 켜짐" : "미리보기: 꺼짐", 410, 506, 340, 15);
  button("지금 곡 인식", 780, 506, 340, 5);
  button("Wi-Fi · AudD", 40, 604, 340, 16);
  label(root, "선택값은 재부팅 후에도 유지됩니다", 24, 410, 618, 800);
  label(root, "미리보기는 직접 작성한 시험 문구이며 실제 인식 결과가 아닙니다",
        24, 40, 710, 1180);
}
static void album(int x, int y, int size) {
  lv_obj_t *tile = lv_obj_create(root);
  art_tile = tile;
  lv_obj_set_pos(tile, x, y);
  lv_obj_set_size(tile, size, size);
  lv_obj_set_style_bg_color(tile, lv_color_hex(0x223C4B), 0);
  lv_obj_set_style_border_width(tile, 0, 0);
  lv_obj_set_style_pad_all(tile, 0, 0);
  lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
  if (artwork.pixels && !audio_quiet) {
    art_image = lv_image_create(tile);
    lv_image_set_src(art_image, &artwork_descriptor);
    int scale_x = size * 256 / artwork.width,
        scale_y = size * 256 / artwork.height;
    int scale = scale_x < scale_y ? scale_x : scale_y;
    lv_image_set_pivot(art_image, 0, 0);
    lv_image_set_scale(art_image, scale);
    lv_obj_set_pos(art_image, (size - artwork.width * scale / 256) / 2,
                   (size - artwork.height * scale / 256) / 2);
  } else {
    default_record = vinyl_create(tile, size);
    if (!default_record) {
      lv_obj_t *l = label(tile, "음악", 32, 0, 0, 0);
      lv_obj_center(l);
    }
    ESP_LOGI(TAG, "Album placeholder: rotating vinyl (%s)",
             audio_quiet ? "quiet input" : "no artwork");
  }
}
static void lyric_label(int index, int x, int y, int width, int size) {
  lyrics_labels[index] = label(root, "", size, x, y, width);
  lv_label_set_long_mode(lyrics_labels[index], LV_LABEL_LONG_DOT);
  lv_obj_set_height(lyrics_labels[index], size >= 48   ? 120
                                          : size >= 32 ? 52
                                                       : 40);
  if (index != 3)
    lv_obj_set_style_text_color(lyrics_labels[index], lv_color_hex(0x7C91A3),
                                0);
}
static void build(void) {
  if (root)
    lv_obj_delete(root);
  default_record = NULL;
  memset(analog, 0, sizeof(analog));
  memset(digital, 0, sizeof(digital));
  memset(lyrics_labels, 0, sizeof(lyrics_labels));
  title_label = artist_label = album_label = status_label = art_image = NULL;
  previous_line = -999;
  chrome_count = 0;
  recognize_button = art_tile = hit_area = NULL;
  root = lv_obj_create(lv_screen_active());
  lv_obj_remove_style_all(root);
  lv_obj_set_style_text_font(root, ui_font(24), 0);
  lv_obj_set_size(root, 1280, 800);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  if (cfg.screen == 3 && !settings_page) {
    uint32_t base = 0x163344;
    if (artwork.pixels) {
      unsigned red = 0, green = 0, blue = 0, count = 0;
      const uint16_t *pixels = (const uint16_t *)artwork.pixels;
      size_t total = (size_t)artwork.width * artwork.height;
      size_t step = total / 64;
      if (!step)
        step = 1;
      for (size_t i = 0; i < total; i += step) {
        uint16_t color = pixels[(i / artwork.width) * (artwork.stride / 2) +
                                (i % artwork.width)];
        red += ((color >> 11) & 31) * 255 / 31;
        green += ((color >> 5) & 63) * 255 / 63;
        blue += (color & 31) * 255 / 31;
        count++;
      }
      base = ((red / count) << 16) | ((green / count) << 8) | (blue / count);
    }
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(
        root, lv_color_mix(lv_color_hex(base), lv_color_hex(0x0C121A), 70), 0);
    lv_obj_set_style_bg_grad_color(root, lv_color_hex(0x0C121A), 0);
    lv_obj_set_style_bg_grad_dir(root, LV_GRAD_DIR_VER, 0);
  }
  if (settings_page == 2) {
    credentials();
    return;
  }
  if (settings_page == 1) {
    settings();
    return;
  }
  label(root, screen_names[cfg.screen], 32, 32, 25, 220);
  status_label = label(root, "연결 확인 중…", 24, 270, 33, 330);
  lv_label_set_long_mode(status_label, LV_LABEL_LONG_DOT);
  recognize_button = button("다시 인식", 620, 20, 170, 5);
  lv_obj_set_style_bg_color(recognize_button, lv_color_hex(0x24786E), 0);
  button(cfg.style ? "디지털" : "아날로그", 810, 20, 140, 1);
  button(cfg.style ? digital_names[cfg.digital_theme]
                   : analog_names[cfg.analog_theme],
         970, 20, 140, 2);
  button("설정", 1130, 20, 118, 3);
  if (cfg.screen < 2) {
    int h = cfg.screen == 0 ? 320 : 470;
    for (int i = 0; i < 2; i++) {
      if (cfg.style) {
        int dh = cfg.screen == 0 ? 148 : 228;
        digital[i] = digital_meter_create(root, 1168, dh, cfg.digital_theme,
                                          i ? "R" : "L");
        if (digital[i])
          lv_obj_set_pos(digital_meter_object(digital[i]), 56,
                         110 + i * (dh + 20));
      } else {
        analog[i] =
            analog_meter_create(root, 592, h, i ? "R" : "L", cfg.analog_theme);
        if (analog[i])
          lv_obj_set_pos(analog_meter_object(analog[i]), 32 + i * 624, 110);
      }
    }
  }
  if (cfg.screen == 0) {
    album(32, 482, 200);
    title_label = label(root, "음악을 듣는 중…", 32, 270, 482, 960);
    artist_label = label(root, "", 24, 270, 535, 960);
    lyric_label(3, 270, 592, 950, 32);
    lyric_label(4, 270, 650, 950, 24);
  } else if (cfg.screen == 2) {
    album(32, 120, 360);
    title_label = label(root, "음악을 듣는 중…", 32, 32, 505, 450);
    artist_label = label(root, "", 24, 32, 565, 450);
    album_label = label(root, "", 24, 32, 615, 450);
    for (int i = 0; i < 7; i++)
      lyric_label(i, 520, 130 + i * 76, 700, i == 3 ? 32 : 24);
  } else if (cfg.screen == 3) {
    lyric_label(2, 60, 230, 1160, 32);
    lyric_label(3, 60, 330, 1160, 48);
    lyric_label(4, 60, 460, 1160, 32);
    for (int i = 2; i <= 4; i++)
      lv_obj_set_style_text_align(lyrics_labels[i], LV_TEXT_ALIGN_CENTER, 0);
    album(32, 540, 180);
    title_label = label(root, "음악을 듣는 중…", 24, 244, 616, 990);
  }
  if (title_label)
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
  if (artist_label)
    lv_label_set_long_mode(artist_label, LV_LABEL_LONG_DOT);
  lv_obj_t *hit = lv_obj_create(root);
  hit_area = hit;
  lv_obj_remove_style_all(hit);
  lv_obj_set_pos(hit, 16, 100);
  lv_obj_set_size(hit, 1248, 610);
  lv_obj_clear_flag(hit, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(hit, touch, LV_EVENT_ALL, NULL);
  for (int i = 0; i < 4; i++)
    button(screen_names[i], 32 + i * 312, 734, 280, 100 + i);
  for (uint32_t n = 0; n < lv_obj_get_child_count(root); n++) {
    lv_obj_t *child = lv_obj_get_child(root, n);
    bool keep = child == title_label || child == art_tile || child == hit_area;
    for (int i = 0; i < 2; i++)
      if (child == analog_meter_object(analog[i]) ||
          child == digital_meter_object(digital[i]))
        keep = true;
    for (int i = 0; i < 7; i++)
      if (child == lyrics_labels[i])
        keep = true;
    if (!keep && chrome_count < 32) {
      chrome[chrome_count++] = child;
      if (idle_hidden) {
        lv_obj_set_style_opa(child, 0, 0);
        lv_obj_add_flag(child, LV_OBJ_FLAG_HIDDEN);
      }
    }
  }
  ESP_LOGI(TAG, "screen=%d style=%d theme=%d", cfg.screen, cfg.style,
           cfg.style ? cfg.digital_theme : cfg.analog_theme);
}
static void translate_y(void *obj, int32_t y) {
  lv_obj_set_style_translate_y(obj, y, 0);
}
static void clear_artwork(void) {
  if (art_image)
    lv_image_set_src(art_image, NULL);
  if (artwork.pixels) {
    lv_image_cache_drop(&artwork_descriptor);
    free(artwork.pixels);
  }
  artwork = (music_art_t){0};
}
static void update_text(void) {
  music_snapshot_t snapshot;
  music_service_snapshot(&snapshot);
  if (snapshot.recognized_us != last_recognized_us) {
    last_recognized_us = snapshot.recognized_us;
    quiet_since = 0;
    if (audio_quiet && artwork.pixels && !settings_page) rebuild = true;
    audio_quiet = false;
  }
  if (snapshot.track_id != displayed_track_id) {
    displayed_track_id = snapshot.track_id;
    clear_artwork();
    if (!settings_page)
      rebuild = true;
  }
  if (settings_page)
    return;
  if (recognize_button) {
    lv_obj_clear_state(recognize_button, LV_STATE_DISABLED);
    set_text(lv_obj_get_child(recognize_button, 0),
             snapshot.request_pending ? "예약됨"
             : snapshot.busy          ? "다시 요청"
                                      : "다시 인식");
  }
  char status[160];
  snprintf(status, sizeof(status), "%s · %s",
           snapshot.connected ? "Wi-Fi 연결" : "오프라인", snapshot.status);
  set_text(status_label, status);
  set_text(title_label,
           snapshot.has_track ? snapshot.title : "음악을 듣는 중…");
  char artist[512];
  snprintf(artist, sizeof(artist), "%s%s%s", snapshot.artist,
           snapshot.album[0] ? " · " : "", snapshot.album);
  set_text(artist_label, artist);
  set_text(album_label, snapshot.release);
  for (int i = 0; i < 7; i++)
    if (lyrics_labels[i]) {
      const char *text = snapshot.lines[i];
      if (i == 3 && !*text) {
        if (snapshot.lyrics_ended)
          text = "가사가 끝났어요 · 다음 곡을 확인하는 중…";
        else if (snapshot.synced && snapshot.line_index >= 0)
          text = "연주 중…";
        else if (snapshot.busy)
          text = "음악을 확인하는 중…";
        else
          text = snapshot.has_track
                     ? (snapshot.synced ? "가사 시작을 기다리는 중…"
                                        : "가사 정보 없음")
                     : "음악을 듣는 중…";
      }
      set_text(lyrics_labels[i], text);
    }
  if (snapshot.line_index != previous_line && cfg.screen == 3 &&
      lyrics_labels[3]) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, lyrics_labels[3]);
    lv_anim_set_values(&a, 18, 0);
    lv_anim_set_duration(&a, 240);
    lv_anim_set_exec_cb(&a, translate_y);
    lv_anim_start(&a);
  }
  previous_line = snapshot.line_index;
}
static void frame(lv_timer_t *t) {
  (void)t;
  if (rebuild) {
    rebuild = false;
    build();
    last_text = 0;
  }
  int64_t now = esp_timer_get_time();
  audio_in_status_t audio = audio_in_status();
  if (audio.block_count && audio.level.rms_dbfs < -55) {
    if (!quiet_since) quiet_since = now;
  } else {
    quiet_since = 0;
  }
  bool quiet = quiet_since && now - quiet_since >= 10000000;
  if (quiet != audio_quiet) {
    audio_quiet = quiet;
    if (artwork.pixels && !settings_page && cfg.screen != 1)
      rebuild = true;
  }
  vinyl_update(default_record, now);
  float dt = (now - last_frame) / 1000000.0f;
  last_frame = now;
  if (wake_gesture && now >= ignore_actions_until) {
    bool pressed = false;
    for (lv_indev_t *input = lv_indev_get_next(NULL); input;
         input = lv_indev_get_next(input))
      if (lv_indev_get_state(input) == LV_INDEV_STATE_PRESSED)
        pressed = true;
    if (!pressed)
      wake_gesture = false;
  }
  if (!settings_page && !idle_hidden && now - last_activity >= 10000000) {
    idle_hidden = true;
    fading_out = true;
    fade_deadline = now + 900000;
    for (unsigned i = 0; i < chrome_count; i++)
      animate_chrome(chrome[i], 0, 900);
    ESP_LOGI(TAG, "Idle fade: retaining meters, lyrics, title and album art");
  }
  if (fading_out && now >= fade_deadline) {
    for (unsigned i = 0; i < chrome_count; i++)
      lv_obj_add_flag(chrome[i], LV_OBJ_FLAG_HIDDEN);
    fading_out = false;
  }
  if (!settings_page && cfg.screen < 2) {
    audio_in_status_t st = audio;
    float amplitude = vu_motion_step(
        &needle, vu_motion_target(st.level.rms_dbfs, cfg.vu_ref), dt);
    digital_motion_step(&leds, st.level.peak_dbfs - cfg.vu_ref, dt);
    for (int i = 0; i < 2; i++) {
      if (cfg.style)
        digital_meter_update(digital[i], leds.level_db, leds.peak_db);
      else
        analog_meter_update(analog[i], amplitude, st.level.peak_dbfs >= -0.5f);
    }
  }
  if (now - last_text >= 100000) {
    last_text = now;
    app_config_t latest = app_settings_get();
    bool layout_changed = latest.screen != cfg.screen ||
                          latest.style != cfg.style ||
                          latest.analog_theme != cfg.analog_theme ||
                          latest.digital_theme != cfg.digital_theme;
    if (settings_page && (latest.lyric_delay_ms != cfg.lyric_delay_ms ||
                          latest.vu_ref != cfg.vu_ref))
      layout_changed = true;
    if (latest.brightness != cfg.brightness)
      board_backlight_set(latest.brightness);
    cfg = latest;
    if (layout_changed)
      rebuild = true;
    update_text();
    music_art_t incoming;
    if (music_service_take_art(&incoming)) {
      if (incoming.track_id != displayed_track_id) {
        free(incoming.pixels);
        return;
      }
      clear_artwork();
      artwork = incoming;
      // A newly downloaded cover takes precedence over an earlier quiet-input state.
      quiet_since = 0;
      audio_quiet = false;
      ESP_LOGI(TAG, "Album artwork received: replacing vinyl, track=%u",
               (unsigned)incoming.track_id);
      artwork_descriptor =
          (lv_image_dsc_t){.header = {.magic = LV_IMAGE_HEADER_MAGIC,
                                      .cf = LV_COLOR_FORMAT_RGB565,
                                      .w = artwork.width,
                                      .h = artwork.height,
                                      .stride = artwork.stride},
                           .data_size = artwork.stride * artwork.height,
                           .data = artwork.pixels};
      if (!settings_page)
        rebuild = true;
    }
  }
  updates++;
  if (now - report_time >= 5000000) {
    float seconds = (now - report_time) / 1000000.0f;
    ESP_LOGI(TAG, "render %.1f fps, updates %.1f Hz", rendered / seconds,
             updates / seconds);
    report_time = now;
    rendered = updates = 0;
  }
}
static void render_done(lv_event_t *e) {
  (void)e;
  rendered++;
}
void music_ui_start(lv_display_t *display) {
  ui_font_init();
  digital_motion_init(&leds);
  cfg = app_settings_get();
  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x0C121A), 0);
  lv_obj_set_style_pad_all(lv_screen_active(), 0, 0);
  lv_obj_clear_flag(lv_screen_active(), LV_OBJ_FLAG_SCROLLABLE);
  build();
  last_activity = esp_timer_get_time();
  for (lv_indev_t *input = lv_indev_get_next(NULL); input;
       input = lv_indev_get_next(input)) {
    if (lv_indev_get_display(input) == display)
      lv_indev_add_event_cb(input, activity, LV_EVENT_ALL, NULL);
  }
  last_frame = report_time = esp_timer_get_time();
  lv_display_add_event_cb(display, render_done, LV_EVENT_RENDER_READY, NULL);
  lv_timer_create(frame, 16, NULL);
}
