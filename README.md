# ESP32-P4 Music Display

10.1인치 화면에 VU 미터, 곡 정보와 가사를 표시하는 ESP-IDF 프로젝트입니다.
[사양서](docs/SPEC.md)의 마일스톤 순서(M0 → M7)로 진행합니다.

보드: **GUITION JC8012P4A1** (ESP32-P4 v3.2, JD9365 800×1280 MIPI-DSI, GSL3680 터치, ES8311, ESP32-C6).
자세한 내용은 [보드 확인표](docs/hardware.md)를 참고하세요.

진행 상황: **아날로그 5종 / 디지털 3종 미터, 4개 화면, 설정 저장, 실제 곡 인식·시간 가사·앨범 아트 연결**.
디지털 미터는 바뀐 세그먼트만 갱신해 실기 약 48~50fps를 확인했습니다.
기본 감도는 −27dBFS=0VU, 가사 지연은 +6.4초이며 NVS 설정에서 조절할 수 있습니다.
신형 패널 프로파일(80MHz·1500Mbps·V4/10/30·RGB888 출력)을 적용해 가는 줄·굵은 선과 화면 감김을 해결했고, 256줄 임시 보정은 제거했습니다.
프리텐다드 Regular를 사용하며 표지가 없거나 무음일 때 회전 LP를 표시합니다. 표지 다운로드는 자동 재시도하며 수신 후 실제 표지로 전환합니다.
조작과 연결 방법은 [사용법](docs/usage.md)을 참고하세요.
상세 내용은 [작업 기록](docs/progress.md)에, 다른 코딩 에이전트용 안내는 [AGENTS.md](AGENTS.md)에 있습니다.

## 구조

```
main/                      앱 진입점 (M0 화면, 터치 로그)
components/board/          보드 초기화: DSI 패널, LVGL 포트, 터치, 백라이트 (핀은 Kconfig)
components/audio_in/       ES8311 마이크 입력, I2S, PSRAM 링 버퍼 (audio_ring.c는 순수 C)
components/level_meter/    DC 차단 필터, RMS/피크 dBFS, 바늘 물리 (순수 C)
components/ui/             PSRAM 눈금판 캐시를 사용하는 아날로그 미터
test/                      PC 단위 테스트: make -C test
components/board/jc8012_panel_init.h  패널 초기화 명령표 (출하 펌웨어 복원본 기본 + 데모 표)
components/esp_lvgl_port/  esp_lvgl_port 2.9.0 사본 (IDF 6 DSI 콜백 패치)
components/esp_lcd_touch_gsl3680/  터치 드라이버 (제조사 데모, 버그 수정 포함)
components/THIRD_PARTY.md  외부 코드 출처와 라이선스
```

## 빌드와 업로드 (macOS)

기본 빌드 환경은 ESP-IDF **v6.0.2**입니다(칩 v3.x 지원, 제조사 펌웨어와 같은 버전). 이 Mac에는 `~/esp/esp-idf-v6.0`에 설치되어 있습니다.
공식 안내: <https://docs.espressif.com/projects/esp-idf/en/v6.0.2/esp32p4/get-started/index.html>

```sh
. ~/esp/esp-idf-v6.0/export.sh
git clone https://github.com/fbrown35/uv-lyric-meter.git
cd uv-lyric-meter
idf.py -B build-idf6 -D SDKCONFIG=sdkconfig.idf6 build
idf.py -B build-idf6 -D SDKCONFIG=sdkconfig.idf6 -p /dev/cu.usbmodem83201 flash monitor
```

처음 빌드하면 `sdkconfig.defaults`와 `dependencies.lock`을 기준으로 설정과 관리 컴포넌트를 생성합니다.
USB 포트 이름은 Mac마다 다르므로 `/dev/cu.usbmodem*`를 확인해 명령에 넣으세요.

모니터 종료는 `Ctrl+]`입니다. 보드 설정(핀, DSI 속도, 회전, 터치 미러, 자가진단)은
`idf.py -B build-idf6 -D SDKCONFIG=sdkconfig.idf6 menuconfig` → *Board (GUITION JC8012P4A1)* 에 있습니다.

### 연결과 테스트

```sh
cp .secrets.example.json .secrets.json
chmod 600 .secrets.json
# 로컬 파일에 Wi-Fi 이름·비밀번호와 AudD 토큰을 입력한 후:
python tools/provision.py
make -C test
```

실제 비밀값과 보드 플래시/NVS 백업은 저장소에 올리지 않습니다.

### 기본 펌웨어 백업·복원

`backup/factory_flash_16MB.bin`이 플래시 전체 백업입니다(Git 제외, 체크섬은 `backup/SHA256SUMS`).

```sh
python -m esptool --chip esp32p4 -p /dev/cu.usbmodem83201 -b 921600 write_flash 0 backup/factory_flash_16MB.bin
```

## 비밀 정보와 라이선스

Wi-Fi 비밀번호와 AudD 토큰은 코드·기본 설정·로그에 넣지 않습니다. 생성된 `sdkconfig`는 Git에서 제외합니다.
가사는 개인용 표시를 전제로 하며, 상품화할 경우 가사 표시의 저작권 라이선스 검토가 필요합니다.
터치 드라이버의 `gsl_point_id.c`는 GPL-2.0+ 표기가 있어 펌웨어 배포 시 검토가 필요합니다.
폰트를 추가할 때 OFL 등 해당 라이선스 파일도 함께 보관합니다.
