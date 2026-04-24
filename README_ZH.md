# ESP-OpenMV

[![OpenMV](https://img.shields.io/badge/OpenMV-4.8.1-00A3E0)](https://github.com/openmv/openmv/tree/v4.8.1)
[![MicroPython](https://img.shields.io/badge/MicroPython-v1.27.0-2B2728)](https://github.com/openmv/micropython/tree/366b6bd242fa068e4ee03a5c516e8cfa7d10c374)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5.3-E7352C)](https://github.com/espressif/esp-idf/releases/tag/v5.5.3)

[English](README.md)

ESP-OpenMV 将 [OpenMV](https://github.com/openmv/openmv) 生态引入 ESP32，让开发者可以结合 MicroPython 的易用性和 Espressif 硬件的可靠性，构建智能、联网的视觉项目。

| AprilTag | Canny |
| --- | --- |
| ![AprilTag demo](https://dl.espressif.com/AE/esp-iot-solution/openmv/apriltag.gif) | ![Canny demo](https://dl.espressif.com/AE/esp-iot-solution/openmv/canny.gif) |
| Color Detect | QR Code |
| ![Color detection demo](https://dl.espressif.com/AE/esp-iot-solution/openmv/color_detect.gif) | ![QR code demo](https://dl.espressif.com/AE/esp-iot-solution/openmv/qrcode.gif) |

## 已支持开发板

| 开发板 | 状态 |
| --- | --- |
| ESP32-P4X-EYE | 已支持 |
| ESP32-P4X-FUNCTION-EV-BOARD | 已支持 |

## 已支持功能

### 核心运行环境

- OpenMV IDE 连接和脚本执行
- OpenMV 风格的 MicroPython soft reset 循环
- 用于 OpenMV IDE 的 USB CDC debug 通道
- 内部 `/flash` 文件系统
- 通过 USB MSC 导出内部 `/flash`
- 通过 `machine.SDCard` 挂载 `/sdcard`

### 摄像头

- 摄像头支持基于 `esp_video`
- PPA crop、scale、mirror、flip pipeline
- `sensor.snapshot()`
- `sensor.skip_frames()`
- `sensor.set_pixformat(sensor.RGB565)`
- `sensor.set_pixformat(sensor.GRAYSCALE)`
- `sensor.set_framesize(sensor.QVGA)`
- `sensor.set_framesize(sensor.QQVGA)`
- `sensor.set_hmirror()` / `sensor.set_vflip()`
- OpenMV IDE 预览需要通过 `img.flush()` 手动刷新

### 显示

- `ESP32-P4X-EYE` 支持通过 OpenMV `display` 模块驱动 LCD
- `display.ESP32Display`
- RGB565 LCD 输出
- 背光控制

### 图像处理

- 从 `/flash` 和 `/sdcard` 加载图像文件
- 基础绘图 API：line、rectangle、circle、ellipse、string、cross、arrow
- 灰度转换
- 二值化阈值处理
- 帧差
- 色块检测
- Canny 边缘检测
- 基础滤波：mean、median、mode、midpoint、morph、gaussian、laplacian、bilateral
- 反色
- QR code 检测
- Barcode 检测
- Template matching
- AprilTag 检测，姿态计算可通过 `pose=False` 关闭

### Wi-Fi

- MicroPython `network.WLAN`
- Wi-Fi STA 连接
- Python 层 HTTP MJPEG 预览示例

注意：长时间运行 Python 网络服务可能影响 OpenMV IDE 连接稳定性，因为 Python 脚本会占用主执行循环。

## ESP Launchpad

ESP Launchpad 烧录支持后续补充。

## 构建、烧录和监控

本项目将 `lib/micropython` 作为上游 submodule，不在仓库内长期维护 MicroPython fork。发布或从上游 MicroPython 重新构建前，需要将本仓库的 `micropython.patch` 打到 `lib/micropython`：

```bash
git submodule update --init --depth=1 --no-single-branch
git -C lib/micropython submodule update --init --depth=1
git -C lib/micropython apply --check ../../micropython.patch
git -C lib/micropython apply ../../micropython.patch
```

使用 ESP-IDF `v5.5.3` tag：

```bash
cd /path/to/esp-idf
git checkout v5.5.3
./install.sh
source ./export.sh
```

构建：

```bash
make -j$(nproc) TARGET=ESP32_P4X_EYE
make -j$(nproc) TARGET=ESP32_P4X_FUNCTION_EV_BOARD
```

烧录：

```bash
make TARGET=ESP32_P4X_EYE ESPPORT=/dev/ttyACM0 deploy
make TARGET=ESP32_P4X_FUNCTION_EV_BOARD ESPPORT=/dev/ttyACM0 deploy
```

监控串口：

```bash
make TARGET=ESP32_P4X_EYE ESPPORT=/dev/ttyACM0 monitor
make TARGET=ESP32_P4X_FUNCTION_EV_BOARD ESPPORT=/dev/ttyACM0 monitor
```

请根据主机实际设备节点修改 `ESPPORT`。如需指定烧录或监控波特率，也可以传入 `ESPBAUD`。

## 板卡适配流程

新增开发板时，优先参考已有 ESP32-P4 开发板。

1. `[MicroPython]` 在 `lib/micropython/ports/esp32/boards/<TARGET>` 添加 MicroPython board 目录，包括 `board.json`、`mpconfigboard.cmake`、`mpconfigboard.h`、`sdkconfig.board` 和分区表。
2. `[MicroPython]` 将板级 ESP-IDF 默认配置写入 `lib/micropython/ports/esp32/boards/<TARGET>/sdkconfig.board`，例如 flash size、partition table path、PSRAM 或组件 Kconfig 选项。
3. `[MicroPython]` 如需共享 ESP32 board sdkconfig fragment，将其放在 `lib/micropython/ports/esp32/boards/`，例如 `lib/micropython/ports/esp32/boards/sdkconfig.<board>`。
4. `[OpenMV]` 在 `boards/<TARGET>` 添加 OpenMV board 目录，包括 `omv_boardconfig.h`、`omv_boardconfig.mk`、`omv_pins.h`、`manifest.py`，以及 camera、SD card、display 的板级 hooks。
5. `[OpenMV]` 板级 pinmux、reset、power 和外设策略应放在 `boards/<TARGET>`。除非行为对所有 ESP32 OpenMV board 都通用，否则不要把板级引脚假设写入 `ports/esp32`。
6. `[OpenMV]` 优先 bring up 存储。根据板卡 SD 模式实现 `boards/<TARGET>/omv_sdcard_board.c`，并验证 `/sdcard` 挂载、文件写入、读回、列目录和删除。
7. `[OpenMV]` 通过 `esp_video` bring up 摄像头。在 `boards/<TARGET>/omv_boardconfig.h` 和 `boards/<TARGET>/omv_camera_board.c` 中填写 sensor ID、active crop window、输入分辨率、QQVGA/QVGA 输出尺寸、SCCB 策略和可选 XCLK 策略。
8. `[OpenMV]` 显示支持保持可选。如果 LCD 尚未 ready，`boards/<TARGET>/omv_display_board.c` 应提供 stub，并返回 `ESP_ERR_NOT_SUPPORTED`，不要部分初始化 panel 硬件。
9. `[Repo]` 在仓库根目录执行 `make -j$(nproc) TARGET=<TARGET>` 构建，然后测试 OpenMV IDE 连接、`sensor.snapshot()`、`img.flush()`、SD 卡访问，以及 `ESP-OPENMV-FUNCTION-TESTS.md` 中的脚本。

## 示例

参考 [ESP-OPENMV-FUNCTION-TESTS.md](ESP-OPENMV-FUNCTION-TESTS.md)，里面包含 camera、image processing、AprilTag、Wi-Fi preview 和 LCD 测试脚本。

## 当前限制

- 摄像头输出当前限制为 `RGB565` / `GRAYSCALE` 和 `QVGA` / `QQVGA`。
- LCD 当前仅在 `ESP32-P4X-EYE` 上启用；`ESP32-P4X-FUNCTION-EV-BOARD` 暂未启用 display 支持。
- 尚未系统性验证全部 OpenMV image API。
- Wi-Fi MJPEG 预览当前由 Python 脚本实现，长时间运行可能影响 OpenMV IDE 响应。
