# ESP-OpenMV

[![OpenMV](https://img.shields.io/badge/OpenMV-4.8.1-00A3E0)](https://github.com/openmv/openmv/tree/v4.8.1)
[![MicroPython](https://img.shields.io/badge/MicroPython-v1.27.0-2B2728)](https://github.com/openmv/micropython/tree/366b6bd242fa068e4ee03a5c516e8cfa7d10c374)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5.3-E7352C)](https://github.com/espressif/esp-idf/releases/tag/v5.5.3)

[中文说明](README_ZH.md)

ESP-OpenMV brings the power of the [OpenMV](https://github.com/openmv/openmv) ecosystem to ESP32, enabling developers to build intelligent, connected vision projects with the simplicity of MicroPython and the robustness of Espressif hardware.

| AprilTag | Canny |
| --- | --- |
| ![AprilTag demo](https://dl.espressif.com/AE/esp-iot-solution/openmv/apriltag.gif) | ![Canny demo](https://dl.espressif.com/AE/esp-iot-solution/openmv/canny.gif) |
| Color Detect | QR Code |
| ![Color detection demo](https://dl.espressif.com/AE/esp-iot-solution/openmv/color_detect.gif) | ![QR code demo](https://dl.espressif.com/AE/esp-iot-solution/openmv/qrcode.gif) |

## Supported Boards

| Board | Status |
| --- | --- |
| ESP32-P4X-EYE | Supported |
| ESP32-P4X-FUNCTION-EV-BOARD | Supported |

## Features

### Core Runtime

- OpenMV IDE connection and script execution
- OpenMV-style MicroPython soft reset loop
- USB CDC debug channel for OpenMV IDE
- Internal `/flash` filesystem
- USB MSC export of internal `/flash`
- `/sdcard` local storage through `machine.SDCard`

### Camera

- Camera support based on `esp_video`
- PPA crop, scale, mirror, and flip pipeline
- `sensor.snapshot()`
- `sensor.skip_frames()`
- `sensor.set_pixformat(sensor.RGB565)`
- `sensor.set_pixformat(sensor.GRAYSCALE)`
- `sensor.set_framesize(sensor.QVGA)`
- `sensor.set_framesize(sensor.QQVGA)`
- `sensor.set_hmirror()` / `sensor.set_vflip()`
- Manual IDE preview refresh through `img.flush()`

### Display

- LCD support through the OpenMV `display` module on ESP32-P4X-EYE
- `display.ESP32Display`
- RGB565 LCD output
- Backlight control

### Image Processing

- Image file loading from `/flash` and `/sdcard`
- Basic drawing APIs: line, rectangle, circle, ellipse, string, cross, arrow
- Grayscale conversion
- Binary thresholding
- Frame difference
- Color blob detection
- Canny edge detection
- Basic filters: mean, median, mode, midpoint, morph, gaussian, laplacian, bilateral
- Negate
- QR code detection
- Barcode detection
- Template matching
- AprilTag detection with optional pose calculation disabled by `pose=False`

### Wi-Fi

- MicroPython `network.WLAN`
- Wi-Fi STA connection
- Python-level HTTP MJPEG preview example

Note: long-running Python network services may make the OpenMV IDE connection less stable because the Python script occupies the main execution loop.

## ESP Launchpad

ESP Launchpad flashing support will be added later.

## Build, Flash, and Monitor

This project keeps `lib/micropython` as an upstream submodule and applies local MicroPython changes through `micropython.patch`. Apply the patch before building:

```bash
git submodule update --init --depth=1 --no-single-branch
git -C lib/micropython submodule update --init --depth=1
git -C lib/micropython apply --check ../../micropython.patch
git -C lib/micropython apply ../../micropython.patch
```

Use the ESP-IDF `v5.5.3` tag:

```bash
cd /path/to/esp-idf
git checkout v5.5.3
./install.sh
source ./export.sh
```

Build:

```bash
make -j$(nproc) TARGET=ESP32_P4X_EYE
make -j$(nproc) TARGET=ESP32_P4X_FUNCTION_EV_BOARD
```

Flash:

```bash
make TARGET=ESP32_P4X_EYE ESPPORT=/dev/ttyACM0 deploy
make TARGET=ESP32_P4X_FUNCTION_EV_BOARD ESPPORT=/dev/ttyACM0 deploy
```

Monitor:

```bash
make TARGET=ESP32_P4X_EYE ESPPORT=/dev/ttyACM0 monitor
make TARGET=ESP32_P4X_FUNCTION_EV_BOARD ESPPORT=/dev/ttyACM0 monitor
```

Change `ESPPORT` to match your host device node. `ESPBAUD` can also be passed to `deploy` or `monitor` when a custom baud rate is needed.

## Board Porting Flow

Use the existing ESP32-P4 boards as templates when adding a new board.

1. `[MicroPython]` Add the MicroPython board directory at `lib/micropython/ports/esp32/boards/<TARGET>`, including `board.json`, `mpconfigboard.cmake`, `mpconfigboard.h`, `sdkconfig.board`, and the partition table.
2. `[MicroPython]` Put board-specific ESP-IDF defaults in `lib/micropython/ports/esp32/boards/<TARGET>/sdkconfig.board`, for example flash size, partition table path, PSRAM, or component Kconfig options.
3. `[MicroPython]` Add shared ESP32 board sdkconfig fragments under `lib/micropython/ports/esp32/boards/` when needed, for example `lib/micropython/ports/esp32/boards/sdkconfig.<board>`.
4. `[OpenMV]` Add the OpenMV board directory at `boards/<TARGET>`, including `omv_boardconfig.h`, `omv_boardconfig.mk`, `omv_pins.h`, `manifest.py`, and board hooks for camera, SD card, and display.
5. `[OpenMV]` Keep board-specific pinmux, reset, power, and peripheral policy inside `boards/<TARGET>`. Do not add board-specific pin assumptions directly to `ports/esp32` unless the behavior is common to all ESP32 OpenMV boards.
6. `[OpenMV]` Bring up storage first. Implement `boards/<TARGET>/omv_sdcard_board.c` for the board SD mode, then verify `/sdcard` mount, file write, readback, directory listing, and delete.
7. `[OpenMV]` Bring up camera through `esp_video`. Fill the sensor ID, active crop window, input resolution, output QQVGA/QVGA sizes, SCCB policy, and optional XCLK policy in `boards/<TARGET>/omv_boardconfig.h` and `boards/<TARGET>/omv_camera_board.c`.
8. `[OpenMV]` Keep display optional. If LCD is not ready, provide `boards/<TARGET>/omv_display_board.c` as a stub that returns `ESP_ERR_NOT_SUPPORTED` instead of partially initializing panel hardware.
9. `[Repo]` Build from the repository root with `make -j$(nproc) TARGET=<TARGET>`, then test OpenMV IDE connection, `sensor.snapshot()`, `img.flush()`, SD card access, and the scripts in `ESP-OPENMV-FUNCTION-TESTS.md`.

## Examples

See [ESP-OPENMV-FUNCTION-TESTS.md](ESP-OPENMV-FUNCTION-TESTS.md) for camera, image processing, AprilTag, Wi-Fi preview, and LCD examples.

## Current Limitations

- Supported camera output is currently limited to `RGB565` / `GRAYSCALE` and `QVGA` / `QQVGA`.
- LCD is currently supported on `ESP32-P4X-EYE`; `ESP32-P4X-FUNCTION-EV-BOARD` display support is not enabled yet.
- Not all OpenMV image APIs have been systematically verified.
- Wi-Fi MJPEG preview is currently implemented as a Python script; running it for a long time can affect OpenMV IDE responsiveness.
