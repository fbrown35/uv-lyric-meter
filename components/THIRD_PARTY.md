# 외부 코드

| 디렉터리 | 출처 | 라이선스 |
|---|---|---|
| `board/jc8012_panel_init.h` | GUITION JC8012P4A1 데모의 JD9365 패널 초기화 명령표(미러 커밋 `f2777e9`). 드라이버 자체는 레지스트리 `espressif/esp_lcd_jd9365` 2.x 사용 | Apache-2.0 (원본 드라이버 라이선스) |
| `esp_lcd_touch_gsl3680/` | 같은 데모의 `common_components/esp_lcd_touch_gsl3680` | `gsl_point_id.c`: Silead, **GPL-2.0 이상** 표기. 나머지 파일은 라이선스 표기 없음 |

`gsl_point_id.c`가 GPL이므로 이 터치 드라이버를 포함한 펌웨어를 **배포**하려면
GPL 조건(소스 공개 등)을 따라야 합니다. 개인 사용에는 문제가 없지만,
배포·상품화 전에는 라이선스를 다시 검토하거나 드라이버를 교체해야 합니다.
| `esp_lvgl_port/` | Espressif esp-bsp `esp_lvgl_port` 2.9.0 (레지스트리 사본). ESP-IDF 6.0.x에서 DSI 콜백 이름(`on_refresh_done`)이 달라 `src/lvgl8|9/esp_lvgl_port_disp.c` 두 줄만 수정(`Local patch` 주석) | Apache-2.0 (`license.txt`) |

## UI 폰트

- `assets/fonts/Pretendard-Regular.ttf`: Pretendard v1.3.9 공식 배포의 `public/static/alternative/Pretendard-Regular.ttf` 원본, 수정/부분집합 처리 없음(2,725,828 bytes).
- 원본: https://github.com/orioncactus/pretendard/releases/tag/v1.3.9
- 저작권: Kil Hyung-jin. SIL Open Font License1.1, 원문 `assets/fonts/OFL-Pretendard.txt` 동봉.
- 설치/검증: `tools/build_font.py`에 공식 ZIP 전달(fonttools 필요). Regular400/static TrueType 및 한글 완성형11,172자 포함 검사.
- UI14/24/32/48px 공통 적용. 글꼴에 없는 LVGL 아이콘만 기존 Montserrat 보조 폰트 사용.
- 전체 문자 매핑14,336개이며 모든 언어/모든 한자를 포함하는 것은 아님.
