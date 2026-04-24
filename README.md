# ESP-OpenMV

ESP-OpenMV brings OpenMV firmware and MicroPython-based machine vision workflows to Espressif platforms.

## Supported Boards

| Board | Status |
| --- | --- |
| ESP32-P4X-EYE | Supported |

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

- LCD support through the OpenMV `display` module
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
```

Flash:

```bash
make TARGET=ESP32_P4X_EYE ESPPORT=/dev/ttyACM0 deploy
```

Monitor:

```bash
make TARGET=ESP32_P4X_EYE ESPPORT=/dev/ttyACM0 monitor
```

Change `ESPPORT` to match your host device node. `ESPBAUD` can also be passed to `deploy` or `monitor` when a custom baud rate is needed.

## Examples

See [OPENMV-P4X-EYE.md](OPENMV-P4X-EYE.md) for camera, image processing, AprilTag, Wi-Fi preview, and LCD examples.

## Current Limitations

- Only `ESP32-P4X-EYE` is supported.
- Supported camera output is currently limited to `RGB565` / `GRAYSCALE` and `QVGA` / `QQVGA`.
- Not all OpenMV image APIs have been systematically verified.
- Wi-Fi MJPEG preview is currently implemented as a Python script; running it for a long time can affect OpenMV IDE responsiveness.
