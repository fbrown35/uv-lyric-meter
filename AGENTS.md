# AGENTS.md — ESP32-P4 Music Display (uv-lyric-meter)

코딩 에이전트(Codex, Claude Code 등)를 위한 작업 안내서입니다. **사양은 [docs/SPEC.md](docs/SPEC.md)**,
진행 기록은 [docs/progress.md](docs/progress.md), 보드 정보는 [docs/hardware.md](docs/hardware.md)에 있습니다.
작업을 시작하기 전에 이 세 문서를 먼저 읽으세요.

## 최신 표시 설정 — 2026-11-26 (아래 과거 진단 기록보다 우선)

- V2 전체 프로파일 정지 패턴에서 사용자 확인: 굵은 선 사라짐, 모서리/색 순서 정상.
- DPI80MHz / DSI1500Mbps / H20·20·40 / V4·10·30 / RGB565 입력→RGB888 출력 / scan offset0.
- 256줄 보정은 이전 혼합 타이밍의 임시 대응이었음. 현재 조합에서는 필요 없고 복구하지 말 것.
- 가는 줄은 출력888로 개선, 굵은 선/원점은 V2 전체 조합에서 개선. 단일 레지스터가 유일 원인이라고 단정하지 않음.
- 일반 음악 UI에서도 사용자가 선 없음·화면 위치 정상·터치 정상 모두 확인함. 가사 기본/NVS +6400ms, 감도-27 유지.
- RAW_SCAN/HOST_PATTERN/FIFO_PROBE/RAW_RGB888 진단은 정상 앱에서 모두 꺼짐.

## 사용자와 소통

- 사용자에게 하는 설명·질문·요약은 **한국어**로 씁니다.
- 코드 주석은 영어, 화면에 나오는 UI 문구는 한국어입니다(SPEC §3).
- 화면 표시는 에이전트가 볼 수 없습니다. 화면 확인이 필요하면 무엇이 보여야 하는지 구체적으로 적어 사용자에게 묻고,
  가능하면 사진을 요청하세요.

## 현재 상태 (2026-11-25)

| 마일스톤 | 상태 |
|---|---|
| M0 화면·터치 | 가로 배치/네 구석/손가락-점 정렬 확인. 잔여 패널 선은 아래 참고 |
| M1 마이크 | 48kHz, 정상 입력 |
| M2 아날로그 | 300ms/약 1.2% 바늘 응답 호스트 검증, 실기 반응 확인 |
| M3 미터/테마 | 아날로그 5종, 디지털 3종, 두 번 탭/버튼/세로 밀기. 사진 IMG_9545 확인, 최적화 후 디지털 약 48~50fps |
| M4 화면/NVS | 전체/미터/곡 정보/가사 집중, 좌우 밀기/하단 버튼. 재부팅 후 화면·스타일·테마와 설정 유지 확인 |
| M5 곡 인식 | C6 SDIO Wi-Fi IP 및 AudD HTTP 200/곡 매칭 성공. JPEG 하드웨어로 400×400 아트 디코딩 성공 |
| M6 가사 | LRCLIB 시간 가사 수신 및 실제 표시 확인. 사용자와 싱크 조정 중(기본 +6400ms 지연); ±0.5초 달성 최종 확인은 아직 |
| M7 설정/다듬기 | 핵심 설정/NVS, UTF-8 한글 폰트, 오류 상태, 재인식 버튼, 집중 표시 구현. 모든 화면의 최종 육안 QA는 진행 중 |

### 현재 보드와 사용자 선호

- 아날로그 5색/LED 3색, 4개 화면, 실제 곡·가사·아트 및 설정 화면 펌웨어가 올라가 있음.
- NVS 사용자 감도 **-27dBFS=0VU**, 최신 조회 가사 지연 **+6400ms**(양수는 늦춤), 자동 인식 켜짐. 기존 2500ms로 강제로 되돌리지 말 것.
- 가사 조정 이력: 0에서 빠름 → +1000 적용 → 사용자가 늦다고 했다가 빠르다고 정정.
  +500 상태에서 약 2초 이상 빠르다고 하여 최종 +2500 적용. 이력을 오해해 다시 0/500으로 되돌리지 말 것.
- 메인 상단 **다시 인식** 버튼. 인식 중에도 ‘다시 요청’으로 누를 수 있고 추가 한 건을 병합 예약하며 ‘예약됨’으로 표시. 화면 길게 누르기와 설정 버튼도 유지.
- 무조작 10초 뒤 약 0.9초 동안 버튼/상태/보조 정보가 사라짐. **미터·가사·제목·앨범 아트는 유지**.
  한 번 터치하면 약 0.2초 동안 복구. 깨우는 첫 터치는 실행/전환하지 않음. 설정 화면은 자동 숨김 제외.
- 최신 로그에서 idle fade와 controls restored 확인. 숨김 후 화면 사진으로 제목/아트 유지 최종 확인을 요청한 상태.

### 연결과 비밀값

- `.secrets.json`은 사용자 입력 로컬 파일(권한 0600, Git 제외). 내용 출력 금지.
- `tools/provision.py`는 USB로 자격 증명을 NVS에 저장. **코드/sdkconfig에 실제 비밀값 넣지 않음**.
- `tools/set_option.py --lyric-delay-ms 2500`, `--vu-ref -27`, `--get` 등은 비밀값 없이 공개 설정 변경/조회.
  범위 밖 10001ms 요청 거부와 기존 값 유지 실기 검증. 변경 후 UI가 반영함.
- esp_hosted 2.11.0, esp_wifi_remote 1.6.5, cjson 1.7.19~2 (dependencies.lock).
  SDIO CLK18/CMD19/D0..3=14..17/reset54, 4-bit 40MHz. C6 펌웨어는 갱신하지 않았음.
- TLS의 cross-signed 체인 검증 지원을 켜서 AudD 접속 성공. 인증서/호스트명 검증을 끄지 말 것.

### 코드/검증의 주의점

- `ui/music_ui.c`: 화면/제스처/설정/집중 표시, `app_settings`: NVS 및 USB 프로토콜,
  `net`: Wi-Fi/SNTP, `music_service`: AudD/LRCLIB/최근 20곡 캐시/아트 큐.
- `lyrics/lrc.c`, `recognizer/wav.c`, `level_meter/digital_motion.c`는 순수 C.
  호스트/ASan/UBSan 테스트 통과(태그/offset/파서 오류, WAV 저역통과/44.1·48kHz, 피크 홀드/릴리스 포함).
- WAV 다운샘플은 float FIR과 협력적 yield를 사용. 원래 double 루프는 IDLE0 watchdog을 유발했음.
  실기 수정 후 watchdog 없이 곡/시간 가사/아트 성공 확인. `recog_task`에서 긴 CPU 루프는 반드시 양보할 것.
- 폰트: `assets/fonts/Pretendard-Regular.ttf` 공식v1.3.9 Regular 원본 약2.6MB, OFL-Pretendard.txt 보관, tools/build_font.py로 공식ZIP에서 설치/검증.
  공식 Regular TTF 원본을 사용하며 모든 언어/모든 한자까지 포함하지는 않음.
- 최종 앱 약 4.2MB로 6MB 앱 파티션에 들어감. 큰 PCM/WAV/가사/이미지는 PSRAM.
- 야간 자동 밝기 등 선택 기능과 모든 오류 상황에 대한 실기 회귀 검증은 아직 완료로 주장하지 않음.

### 보드 표시 보정 — 유지할 것

DPI 80MHz, DSI 1500Mbps, H20/20/40·V4/10/30, RGB565 입력→RGB888 출력, `BOARD_PANEL_SCAN_OFFSET=0`, 패널 행·열 반전 끔, 회전 270도,
터치 MIRROR_X=y/MIRROR_Y=n, RAW_SCAN_TEST=n, MUSIC_DISPLAY_DIAGNOSTICS=n.
2026-11-26 정지 패턴 및 일반 UI에서 사용자가 선 없음/위치·터치 정상 확인. 이전의256줄 보정은 현재 타이밍에서 사용하지 않음.
NORON/상태 읽기 진단은 빈 화면 회귀 후보라 삭제했으며 재적용하지 말 것.
기존 복구 바이너리는 현재 채팅 work/에 보관. 상세 이력은 docs/progress.md.

### 최신 회귀 수정 (IMG_9546)

사진의 서버 연결 실패는 HTTP 200 후 수동 본문 수신/완료 판정 경로를 표준
`esp_http_client_perform` + bounded event buffer로 교체해 수정했습니다. 최신 실기에서
AudD 4862 bytes, LRCLIB 10691 bytes, 시간 가사 및 400×400 아트 성공 확인.
사용자가 터치 직후 찍은 사진임을 확인했으므로 사진 속 버튼 표시는 idle 실패로 판단하지 마세요.

### IMG_9547 후속 수정

- 마지막 빈 LRC 줄을 ‘가사 정보 없음’으로 표시하지 않음. 가사 종료 시 즉시 다음 곡 요청, 이전 곡이면 앵커를 되감지 않고 새 8초 구간으로 재시도.
- 수동 접수는 연결/소리 준비 중 최대 30초 유지, 매칭 재시도 최대 3회. 인식용 PCM 복사본만 DC 제거/정규화, ADC/미터 감도 유지.
- JPEG 하드웨어 RGB565가 big-endian임을 보드 테스트로 확인. LVGL용 변환 후 R=f800/G=07e0/B=001f PASS. 표지의 원본 치수/stride/종횡비 및 곡 ID 일치 처리, 가사 집중 모드 표지 추가, 설정 중 표지 도착 유실 수정.
- 내부 RAM 부족으로 esp-aes/TLS 할당 실패가 발생했으므로 SPIRAM_MALLOC_ALWAYSINTERNAL=512 및 MBEDTLS_EXTERNAL_MEM_ALLOC=y로 변경. TLS 검증 유지. 수정 후 내부 free 약 43KB/최대 블록 31KB 및 HTTP 200 확인.
- recognizer/pcm_prepare.c, recog_retry.c, music_service/image_pixels.c와 lrc_end_ms의 호스트/ASan/UBSan 테스트 통과.
- 실제 다음 곡 교체, 버튼 예약 및 최종 표지 사진은 음악 재생과 함께 확인해야 함. 현재 사용자에게 음악 10초 이상 재생 후 버튼 시험을 요청한 상태.

### 2026-11-26 가사 누락 진단

- 재인식 버튼의 wake guard 제거 및 클릭 접수 로그 추가. USB CONTROL DIAG/RECOGNIZE는 공개 진단 전용.
- 사용자 가사 미표시 신고 때 LRCLIB 404 및 lyrics=none 확인. 이후 진단 펌웨어에서 AHOF / Run To You 시간 가사 12064 bytes와 400×400 표지 수신, lyrics=1/art=1 확인. 사용자가 곡 일치 및 실제 화면 가사 표시를 확인함.
- SERVICE_TRACK 진단은 현재 곡/가사/표지 상태만 출력. secrets 출력 금지.

- 사용자 확인: 앨범 아트 품질 양호. 가사 지연 +6400ms를 제품 기본값으로 지정함. app_settings 초기값에도 반영하며 기존 NVS 설정은 유지.

### 바로 다음 할 일

1. 최신 저장 지연값(최근 6400ms)을 유지하며 사용자 싱크 확인. USB 옵션으로 100ms 단위 미세 조정 가능.
2. 12초 무조작 화면 사진으로 제목/아트/미터/가사 유지, 터치 복구와 ‘다시 인식’ 버튼 확인.
3. 새 요청이 없으면 현재 동작 설정을 유지하고 오류/장시간 안정성 및 선택 기능을 후속 검증.

## 개발 환경 (사용자 Mac)

- ESP-IDF 두 벌이 설치되어 있습니다.
  - **v6.0.2** `~/esp/esp-idf-v6.0` ← **현재 기본.** 제조사 펌웨어와 같은 버전.
  - v5.5.5 `~/esp/esp-idf` — 빌드는 되지만 더 이상 기본이 아님.
- 칩은 ESP32-P4 **v3.2**입니다. v3.x를 지원하지 않는 IDF(≤5.4)로 빌드하면 안 됩니다.
- 보드 포트: `/dev/cu.usbmodem83201` (USB Serial/JTAG).

### 빌드·업로드 (IDF 6.0.2)

```sh
. ~/esp/esp-idf-v6.0/export.sh
cd ~/Downloads/uv-lyric-meter
idf.py -B build-idf6 -D SDKCONFIG=sdkconfig.idf6 build
idf.py -B build-idf6 -D SDKCONFIG=sdkconfig.idf6 -p /dev/cu.usbmodem83201 -b 921600 flash
```

- 설정 변경: `idf.py -B build-idf6 -D SDKCONFIG=sdkconfig.idf6 menuconfig` → *Board (GUITION JC8012P4A1)*, *Audio input*, *Music display*.
  비대화형 환경에서는 `sdkconfig.idf6`를 직접 고친 뒤 `reconfigure` 하세요. `sdkconfig.defaults`를 바꾸면 새 설정 파일에만 반영됩니다.
- `dependencies.lock`은 현재 IDF 6.0.2 기준입니다. IDF 5.5.5로 빌드하면 다시 써지니 둘을 번갈아 쓰지 마세요.
- 명령은 `< /dev/null`로 실행하세요(대화형 입력을 기다리며 멈추는 것 방지).

### 시리얼 로그 받기 (TTY 없는 환경)

`idf.py monitor`는 TTY가 필요합니다. 대신 pyserial로 리셋 후 일정 시간 읽습니다:

```sh
. ~/esp/esp-idf-v6.0/export.sh
python - <<'EOF'
import serial, time
s = serial.Serial('/dev/cu.usbmodem83201', 115200, timeout=0.2)
s.dtr = False; s.rts = True; time.sleep(0.1); s.rts = False   # reset into app
end = time.time() + 20
while time.time() < end:
    d = s.read(65536)
    if d: print(d.decode(errors='replace'), end='', flush=True)
EOF
```

리셋 없이 듣기만 하려면 `s.open()` 전에 `dtr`/`rts`를 False로 둡니다. 소리·터치 테스트는 녹음 시작 **전에** 사용자에게 무엇을 할지 알려 주세요.

### 호스트 단위 테스트

```sh
make -C test        # level_meter, audio_ring — ESP-IDF 불필요
```

## 코드 구조

```
main/main.c                         M0 화면(제목, 네 구석, 터치 점) + M1 마이크 레벨 로그
components/board/                   보드 초기화(DSI/JD9365, LVGL 포트, GSL3680 터치, 백라이트). 핀·옵션은 Kconfig
components/board/jc8012_panel_init.h 패널 초기화 명령표 2종(factory = 기본, demo)
components/audio_in/                ES8311 + I2S 녹음, PSRAM 12초 링 버퍼(audio_ring.c는 순수 C)
components/level_meter/             20Hz DC 차단, RMS/피크 dBFS(AES17) — 순수 C
components/esp_lvgl_port/           esp_lvgl_port 2.9.0 사본(IDF 6 DSI 콜백 두 줄 패치, "Local patch" 주석)
components/esp_lcd_touch_gsl3680/   제조사 터치 드라이버(버퍼 오버플로 등 수정, GPL 파일 포함)
components/THIRD_PARTY.md           외부 코드 출처·라이선스
test/                               호스트 테스트
backup/                             제조사 펌웨어 16MB 전체 백업(Git 제외) + SHA256SUMS
```

## 꼭 알아야 할 함정 (오늘 실기로 확인한 것)

- **패널 초기화 명령표**: 제조사 데모 저장소의 표로는 줄만 나오고 영상이 안 나옴. 보드 출하 펌웨어(백업, flash 0x6b890)에서
  복원한 표(`jc8012_panel_init_cmds_factory`)로 영상이 나옴. 업스트림 jd9365 2.x 기본 표는 다른 패널용.
- **DSI PHY 클럭**: `MIPI_DSI_PHY_CLK_SRC_DEFAULT`(PLL_F20M)는 v3.x 칩에서 abort. `phy_clk_src = 0`으로 둘 것.
- **DSI 클럭 레인 HS 고정**(`clock_lane_force_hs`)은 버스 초기화에서 멈춤.
- **백라이트 PWM 20kHz는 마이크에 −47 dBFS 톤으로 섞임** → 50kHz(`BOARD_BACKLIGHT_PWM_HZ`)로 변경. 낮추지 마세요.
- 마이크는 I2S **왼쪽 슬롯**, 오른쪽은 0. ES8311 I2C 주소 0x18(esp_codec_dev 표기 0x30).
- esp_lvgl_port 2.9는 LVGL ≥ 9.3, esp_lcd_touch ≥ 1.2 필요.
- IDF 6의 GCC는 LVGL의 IRAM 속성에 `-Wattributes` 경고 → `main/CMakeLists.txt`에서 LVGL만 경고 해제.

## 작업 규칙 (SPEC §14 요약)

- 모르는 하드웨어 값은 추측하지 말고 Kconfig로 빼고 TODO 주석. 기본값의 출처를 help에 적기.
- API 토큰·Wi-Fi 비밀번호는 커밋 금지. `sdkconfig*`는 Git 제외.
- 기능을 넣을 때마다 빌드 확인. 순수 C 모듈은 `test/`에 테스트 추가.
- 큰 버퍼(프레임 버퍼, 링 버퍼, 앨범 아트, 폰트)는 PSRAM, 실시간 오디오용 작은 버퍼는 내부 RAM.
- 외부 API 응답 구조는 공식 문서로 확인하고, 파싱 실패로 앱이 죽지 않게.
- 마일스톤마다 `docs/progress.md`에 바뀐 파일·확인한 내용·남은 문제를 덧붙이기.
- **플래시 쓰기는 보드의 기존 펌웨어를 덮어씁니다.** 이 프로젝트 펌웨어를 올리는 것은 사용자가 승인했지만,
  `erase_flash`나 백업 덮어쓰기처럼 되돌리기 어려운 작업은 먼저 묻기. 제조사 펌웨어 복원:
  `python -m esptool --chip esp32p4 -p /dev/cu.usbmodem83201 -b 921600 write_flash 0 backup/factory_flash_16MB.bin`
- GitHub 저장소: https://github.com/fbrown35/uv-lyric-meter (origin). Git 초기화 및 첫 업로드는 사용자가 요청함. 이후 커밋/업로드도 사용자 요청 범위에서만.

### 가로줄 분리 시험 진행 중 (2026-11-26)

- 현재 보드는 음악 UI로 복구 완료. RGB565 입력→RGB888 출력, DPI80MHz/DSI1500Mbps/offset0/HFP40/V4·10·30. RAW_SCAN/HOST_PATTERN/RAW_RGB888 꺼짐. 복구 앱/설정은 채팅 work/line-test-app-recovery에 보관.
- A raw565/80MHz: 줄 유지. B raw565/60MHz: 비슷함. C DSI host 패턴: 가는 줄/굵은 선 모두 사라짐(IMG_9551). D raw888: 가는 줄 사라짐, 굵은 선 유지. E RGB565 FB→RGB888 DSI: 가는 줄 사라짐, 굵은 선 유지.
- F offset256→0: 굵은 선 같은 패널 위치. G HFP40→48: 비슷함. H lane1000→1500도 비슷함.
- RGB888 출력은 가는 줄 개선이 사용자 확인됨. 기본 꺼진 BOARD_DSI_OUTPUT_RGB888 옵션. 앱에 적용할 때 lane bandwidth를 고려할 것.
- BOARD_DISPLAY_RAW_RGB888/HOST_PATTERN은 진단 전용, 정상 앱 복구 시 모두 끄기. offset256 복구/HFP40 복구. +6400ms 기본/NVS 유지.
- 굵은 선의 근본 원인은 아직 미확정. 하드웨어 불량이나 회전 문제로 확정하지 말 것.

- 웹 조사 새 근거: HomeTiles issue18 공개 V2표204명령과 로컬 factory표가 cmd/data/length/delay 모두 동일. 하지만 vertical porch는 구형8/20 사용 중이며 V2 사례는10/30(80MHz/1500Mbps). 다음 비교 후보는 이 불일치. 자세한 출처는 docs/progress.md.

- 추가 시험 I 결과: VBP10/VFP30에서 사용자 화면 밀림/IMG_9552 확인. VBP8/VFP20으로 복구. 일반 음악 UI/RGB888출력/60MHz/DSI1000/offset256/HFP40 유지. 현재 10/30 단독 조합은 채택하지 않음. 사용자가 8/20 복구 후 원래 위치로 돌아왔음을 확인함. 이전8/20 앱/설정은 work/recovery-app-rgb888-v8-20에 보관.

### 최종 확인 (2026-11-26)

- 사용자: 일반 음악 화면에서 선·화면 위치·터치 모두 정상. 굵은 선 해결 완료.
- 최종 앱 약45~50fps, startup timing log 및 JPEG color test PASS, 가사 저장 지연6400ms 유지.
- 과거8/20,60MHz,DSI1000,offset256 진단/유지 메모는 이 최종 상태 이전의 기록임. 현 설정으로 되돌리지 말 것.

### 기본 앨범 아트 LP (2026-11-26)

- ui/vinyl.c: PSRAM static disc canvas + 두 반사광/중앙 마크,20Hz/8초1회전. album() 공통 적용으로 전체/곡 정보/가사 집중에서 표시.
- 표지 없음 또는 연속10초 rms<-55dBFS면 LP. 저장한 실제 표지는 해제하지 않고 음악 재개 시 복원. root 삭제 시 위젯/PSRAM 해제, idle fade에서도 art tile 유지.
- 빌드/업로드/해시 검증 및 22초 실기 안정 로그 확인. 사용자가 LP 정상 회전을 확인함.

- LP 복귀 보강 완료: 인식 timestamp/표지 수신에서 quiet 상태 초기화, 실패 표지5초 간격 총3회 재시도(추가AudD없음). 사용자가 실제 표지 복귀 확인.

- 프리텐다드 Regular 교체:14/24/32/48px, 아날로그/디지털 미터 포함. 원본TTF 변경/서브셋 없음. 한글 완성형전체 포함검사. 업로드/해시 검증 및 실기4크기 초기화 정상 확인.
