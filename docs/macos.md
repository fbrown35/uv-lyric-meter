# macOS에서 보드 확인하기

사용자는 macOS에 보드를 USB로 연결했습니다. 2026-11-25부터 빌드와 업로드를 이 Mac에서 직접 합니다.
아래 명령은 **Mac의 터미널**에서 실행합니다.
현재 소스에는 M0 화면·터치 펌웨어가 들어 있습니다.

## ESP-IDF와 소스 준비

공식 macOS 설치 안내에 따라 ESP-IDF v5.5.5과 ESP32-P4 도구를 준비합니다.

<https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32p4/get-started/macos-setup.html>

Mac에 ESP-IDF가 이미 설치되어 있다면 기존 환경을 사용합니다.
현재 클라우드 변경사항은 GitHub에 푸시하지 않았으므로 원격 저장소를 클론하는 것만으로는
이 소스를 받을 수 없습니다. 제공한 소스 압축 파일을 내려받아 풀거나 변경사항을 동기화합니다.
클라우드의 Linux 도구 체인, Python 환경과 빌드 폴더는 Mac에 복사하지 않습니다.

아래 IDF 및 소스 경로는 Mac에서 실제 설치·압축 해제한 경로로 바꿉니다.

```sh
. "$HOME/esp/esp-idf/export.sh"
cd /path/to/uv-lyric-meter
idf.py --version
idf.py build
```

## 읽기 전용으로 장치 확인

제품 가이드에서 다운로드용으로 지정한 USB 커넥터와 데이터 케이블을 사용합니다.
이 보드의 여러 Type-C 포트가 모두 동일한 기능을 제공한다고 가정하지 않습니다.

```sh
python -m serial.tools.list_ports -v
```

보드 연결 전후 목록을 비교하여 실제 포트를 찾습니다. 보통 `/dev/cu.usbmodem...`
또는 `/dev/cu.usbserial...` 형태이며 포트명을 추측하지 않습니다.
포트가 없으면 케이블·연결 포트를 확인한 뒤, USB-UART 칩이 식별되었을 때만
해당 제조사 공식 드라이버가 필요한지 확인합니다.

```sh
BOARD_PORT=/dev/cu.ACTUAL_PORT
python -m esptool --chip esp32p4 --port "$BOARD_PORT" flash_id
```

`BOARD_PORT`를 실제 목록의 포트로 바꿉니다. 위 명령은 플래시를 쓰지 않지만
연결 과정에서 보드를 리셋할 수 있습니다.
칩 리비전·플래시 크기와 사용한 USB 포트를 기록해 보드 자료와 대조합니다.

## 화면·터치 구현 후 실기 검증

현재 진단 바이너리를 설치해도 화면이나 미터가 표시되지 않습니다.
검증된 BSP를 통합하고, 실물에 맞는 플래시/PSRAM/콘솔 설정을 적용한 뒤
기존 펌웨어를 보관할 필요가 있는지 확인하고 업로드합니다.

```sh
idf.py menuconfig
idf.py -p "$BOARD_PORT" flash monitor
```

모니터 종료는 `Ctrl+]`입니다. 가로 1280×800 글자 표시와 터치 좌표를
실제 화면에서 확인해야 M0 완료로 기록합니다.
