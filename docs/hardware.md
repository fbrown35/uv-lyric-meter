# M0 보드 확인표

사용자가 올린 인터페이스 이미지의 실크에서 **GUITION JC8012P4A1**을 식별했습니다.
이미지에는 ESP32-P4, ESP32-C6, 마이크, 스피커, RTC, TF와 USB 포트가 표시되어 있습니다.
클라우드 실행 환경에는 USB/시리얼 장치가 노출되어 있지 않습니다.
아래 값은 제품 문서·회로도·공식 BSP·장치 로그로 확인한 뒤 기록합니다.

| 항목 | 확인 상태 |
| --- | --- |
| 제조사 / 모델 / 보드 리비전 | GUITION JC8012P4A1 이미지 식별; C_I_W_Y 등 SKU와 실물 리비전은 미확인 |
| BSP / 예제 | 아래 제조사 자료 미러의 LVGL 9 예제 확인; 실기 동작 미검증 |
| ESP32-P4 실리콘 리비전 | 실기 esptool 확인: ESP32-P4 revision v3.2 (2026-11-25) |
| 실제 플래시 용량 / 모드 | 실기 flash_id: 16MB, 제조사 ID 0x68 / device 0x4018; 동작 모드는 미확인 |
| PSRAM 종류 / 용량 / 설정 | 제품 자료 32MB; 예제 200MHz 설정, 실기 검증 필요 |
| MIPI-DSI 패널 / 전원 / 리셋 / 백라이트 | JD9365, 800×1280; 예제 RST 27 / BL 23 |
| 터치 컨트롤러 / 핀 | GSL3680; 예제 SDA 7 / SCL 8 / RST 22 / INT 21 |
| 오디오 코덱 / 마이크 / I2S 핀 | ES8311(I2C 0x18) + 아날로그 MEMS 마이크 MSM381A3729H9CP(MIC1, 전원은 ADCVREF 경유). MCLK 13 / BCLK 12 / LRCK 10 / DOUT 9 / DIN 11; PA 20. **실기 녹음 확인**: 마이크는 I2S 왼쪽 슬롯, 무음 −62 dBFS |
| Wi-Fi 보조 칩 / esp_hosted 연결 방식 | ESP32-C6; 자료상 SDIO 4-bit ESP-Hosted, 탑재 펌웨어 버전은 실기 확인 필요 |
| USB 커넥터 기능 / 콘솔 포트 | `/dev/cu.usbmodem83201`, USB Serial/JTAG, VID:PID 303A:1001, MAC은 공개 문서에서 생략 |

## 출처 및 적용 범위

- 사용자 첨부 `S72d78d6d9605477d98c8858a80730222g.jpg.avif`: 보드 표기 및 부품 위치.
- [제조사 자료 미러](https://github.com/sukesh-ak/JC8012P4A1-GUITION-ESP32-P4_ESP32-C6),
  확인 커밋 `f2777e9132597d7ade122cc434fce034a4addf4b`.
  `2-Specification/JC8012P4A1C_I_W Specifications-EN-v1.1(2).pdf`와
  `1-Demo/idf-examples/common_components/esp32_p4_function_ev_board/`를 확인했습니다.
  미러 저장소를 제조사 공식 배포처로 단정하지 않습니다.
- [커뮤니티 BSP](https://github.com/profi-max/JC8012P4A1_BSP_ESP32P4),
  확인 커밋 `864d486942f45dc5c4858dcdb4acd5d36cdb49b1`: JD9365/GSL3680/ES8311,
  C6 SDIO 및 핀맵 교차 확인. 해당 저장소는 개발 중이라고 명시합니다.

위 핀은 해당 자료의 값이며 사용자 실물에서 측정한 값은 아닙니다.
현재 진단 펌웨어는 이 핀들을 구동하지 않습니다.
제조사 LVGL 9 예제는 LVGL 9.2 계열과 `esp_lvgl_port`를 사용합니다.
보드 리비전·회로도를 대조한 뒤 필요한 컴포넌트만 연결해야 합니다.

## 확인 후 진행 순서

1. 제조사 예제의 ESP-IDF 버전, LVGL 9 지원, 부품과 핀 배치를 확인합니다.
2. 확인한 BSP를 컴포넌트 매니페스트에 추가하고 보드 설정을 적용합니다.
3. 제조사 권장 전원·패널 초기화 순서를 사용합니다.
4. 가로 1280×800에서 글자를 표시하고 터치 좌표와 회전을 검증합니다.
5. `idf.py build` 및 실기 검증 결과를 작업 기록에 남기고 M1로 진행합니다.

## 2026-11-25 Mac 실기 확인

로컬 Mac에서 esptool 4.14.0의 자동 칩 감지와 `flash_id`를 실행했습니다.
ESP32-P4 v3.2, 40MHz crystal, 16MB flash가 확인됐습니다.
연결 과정에서 RAM stub을 실행하고 리셋했으며 플래시는 쓰거나 지우지 않았습니다.
USB 식별만으로 GUITION 모델명, PSRAM 용량, 패널 및 터치 부품은 확인되지 않습니다.

**기존 ESP-IDF 5.4.3 빌드 사용 금지:** 실물은 v3.2이며 v3.x 지원 IDF가 필요합니다.
[Espressif v3.2 호환성 안내](https://documentation.espressif.com/AR2026-004_Compatibility_Advisory_for_ESP32-P4_Chip_Revision_v3.2_EN.html)를 확인했습니다.
M0 개발 환경은 v5.5.5로 전환하고 `ESP32P4_SELECTS_REV_LESS_V3=n`을 적용했습니다.

기본 펌웨어(백업)는 ESP-IDF v6.0.2, 앱 1.0.1, JD9365 드라이버 2.0.2로 빌드되어 있습니다(부팅 로그).
C6 ESP-Hosted 펌웨어는 2.3.0으로 호스트 2.11.0보다 낮다는 경고가 나옵니다.
2026-11-25 기본 펌웨어에서도 화면에 가로줄이 보임(사용자 확인) → 디스플레이 케이블 점검 필요.

## 2026-11-25 마이크 확인

회로도(`5-Schematic/7_CODEC.png`): ES8311 ASDOUT → GPIO11, MIC1P/N ← MSM381A3729H9CP(N측 0Ω로 GND).
ES7210 넷 이름도 보이지만(같은 I2S 핀 공유) 이 보드에서 실장 여부는 확인하지 않았습니다.
백라이트 PWM을 제조사 값 20kHz로 두면 마이크에 −47 dBFS 20kHz 톤이 섞입니다 → 50kHz로 변경.

## 2026-11-25 ESP-Hosted 실기 확인

- esp_hosted 2.11.0 / esp_wifi_remote 1.6.5로 C6를 인식하고 Wi-Fi IP 할당 성공.
- SDIO: CLK 18, CMD 19, D0 14, D1 15, D2 16, D3 17, C6 reset 54, 4-bit 40MHz.
- 핀 대조 자료: 커뮤니티 BSP 커밋 `864d486942f45dc5c4858dcdb4acd5d36cdb49b1`의 `Simple_WiFi_hosted/sdkconfig.esp32-p4_r3`.
- 기존 C6 펌웨어는 갱신하지 않음. P4 쪽 구성/인식 및 실제 HTTPS 요청 성공으로 통신 경로 확인.
- AudD는 Google Trust Services의 cross-signed 인증서 체인을 제공함. IDF의
  `MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY` 지원을 켜서 검증했으며,
  호스트명 확인/서버 인증서 검증을 끄지 않음.

## 2026-11-26 신형 패널 표시 설정 검증

- 공개 HomeTiles issue18 V2 초기화204명령과 로컬 factory표의 cmd/data/length/delay 전체 일치.
- RGB888 출력으로 미세한 줄 제거, V2 전체 조합(80MHz/1500Mbps/H20·20·40/V4·10·30)에서는 사용자 확인으로 굵은 선도 사라짐.
- 이때 scan offset0에서 모서리/색 순서 정상. 기존256줄 보정 없이 native 원점 정상. 과거 하드웨어 의심은 원인 확정이 아니었으며 현재 소프트웨어 프로파일 개선으로 표시가 정상화된 정지 시험 결과를 우선함.
- 일반 앱 최종 표시/터치 확인 진행 중. 실제 외장 라벨 V2/V3 명칭은 칩 리비전과 별도로 확인해야 함.
- 후속 사용자 최종 확인: 일반 음악 화면에서도 선·화면 위치·터치 모두 정상. 표시 설정 검증 완료.
