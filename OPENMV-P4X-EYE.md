# P4X-EYE-OPENMV

## Table of Contents

- [P4X-EYE-OPENMV](#p4x-eye-openmv)
  - [Table of Contents](#table-of-contents)
  - [0. Build, Flash, and Monitor](#0-build-flash-and-monitor)
  - [1. Camera Test](#1-camera-test)
  - [2. Image Processing](#2-image-processing)
    - [2.1 Basic Preview](#21-basic-preview)
    - [2.2 Drawing API Test](#22-drawing-api-test)
    - [2.3 Image Processing API Test](#23-image-processing-api-test)
      - [2.3.1 Filters](#231-filters)
      - [2.3.2 Color Tracking](#232-color-tracking)
      - [2.3.3 Frame Difference](#233-frame-difference)
    - [2.4 Barcodes API Test](#24-barcodes-api-test)
    - [2.5 Feature Detection](#25-feature-detection)
  - [3. April-Tags](#3-april-tags)
  - [4. Wi-Fi](#4-wi-fi)
    - [4.1 Wi-Fi Image Preview](#41-wi-fi-image-preview)
  - [5. Other](#5-other)
    - [5.1 Peripheral Support](#51-peripheral-support)
      - [5.1.1 LCD Support](#511-lcd-support)

## 0. Build, Flash, and Monitor

This project keeps `lib/micropython` as an upstream submodule and does not maintain a
separate MicroPython fork in-tree. Before building `ESP32_P4X_EYE`, apply the
repository-level `micropython.patch` to `lib/micropython`.

```bash
git submodule update --init --depth=1 --no-single-branch
git -C lib/micropython submodule update --init --depth=1
git -C lib/micropython apply --check ../../micropython.patch
git -C lib/micropython apply ../../micropython.patch
```

Use the ESP-IDF `v5.5.3` tag version.

```bash
cd /path/to/esp-idf
git checkout v5.5.3
./install.sh
source ./export.sh
```

Build firmware:

```bash
make -j$(nproc) TARGET=ESP32_P4X_EYE
```

Flash firmware:

```bash
make TARGET=ESP32_P4X_EYE ESPPORT=/dev/ttyACM0 deploy
```

Monitor serial output:

```bash
make TARGET=ESP32_P4X_EYE ESPPORT=/dev/ttyACM0 monitor
```

`ESPPORT` should be changed to the actual device node on your host. `ESPBAUD` can
also be passed to `deploy` or `monitor` if a custom baud rate is required.

## 1. Camera Test

* Supported resolutions: QVGA, QQVGA
* Supported formats: RGB565, GRAYSCALE
* Supported mirroring: horizontal and vertical

```python
import sensor
import time

sensor.reset()

while True:
    sensor.set_pixformat(sensor.GRAYSCALE)
    sensor.set_framesize(sensor.QQVGA)
    sensor.skip_frames(time=300)
    img = sensor.snapshot()
    print("GRAY QQVGA:", img.width(), img.height(), img.format())
    img.flush()
    time.sleep_ms(1000)

    sensor.set_pixformat(sensor.GRAYSCALE)
    sensor.set_framesize(sensor.QVGA)
    sensor.skip_frames(time=300)
    img = sensor.snapshot()
    print("GRAY QVGA:", img.width(), img.height(), img.format())
    img.flush()
    time.sleep_ms(1000)

    sensor.set_pixformat(sensor.RGB565)
    sensor.set_framesize(sensor.QQVGA)
    sensor.skip_frames(time=300)
    img = sensor.snapshot()
    print("RGB565 QQVGA:", img.width(), img.height(), img.format())
    img.flush()
    time.sleep_ms(1000)

    sensor.set_pixformat(sensor.RGB565)
    sensor.set_framesize(sensor.QVGA)
    sensor.skip_frames(time=300)
    img = sensor.snapshot()
    print("RGB565 QVGA:", img.width(), img.height(), img.format())
    img.flush()
    time.sleep_ms(1000)

```

## 2. Image Processing

### 2.1 Basic Preview

```python
import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)  # or GRAYSCALE
sensor.set_framesize(sensor.QVGA)  # or QQVGA
sensor.skip_frames(time=2000)

while True:
    img = sensor.snapshot()
    img.flush()
    time.sleep_ms(20)

```

> Note: Compared with the original OpenMV examples, you need to manually add `img.flush()` to refresh the image preview.

### 2.2 Drawing API Test

```python
import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1000)

x = 0
dx = 4

while True:
    img = sensor.snapshot()

    img.draw_line(0, 0, img.width() - 1, img.height() - 1, color=(255, 0, 0), thickness=2)
    img.draw_rectangle(20, 20, 80, 60, color=(0, 255, 0), thickness=2)
    img.draw_circle(img.width() // 2, img.height() // 2, 30, color=(0, 0, 255), thickness=2)
    img.draw_cross(img.width() // 2, img.height() // 2, color=(255, 255, 0), size=16, thickness=2)
    img.draw_arrow(10, img.height() - 20, img.width() - 20, 20, color=(255, 0, 255), size=20, thickness=2)

    img.draw_rectangle(x, 100, 40, 40, color=(0, 255, 255), thickness=3)
    img.draw_string(10, 5, "DRAW TEST", color=(255, 255, 255), scale=2)

    img.flush()

    x += dx
    if x <= 0 or x >= (img.width() - 40):
        dx = -dx

    time.sleep_ms(30)
```

### 2.3 Image Processing API Test

#### 2.3.1 Filters

```python
import sensor
import time
import image

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1000)


modes = [
    "mean",
    "median",
    "mode",
    "midpoint",
    "morph",
    "gaussian",
    "laplacian",
    "bilateral",
    "binary",
    "negate",
]

mode = 0
last_switch = time.ticks_ms()

kernel = [0, -1, 0, -1, 5, -1, 0, -1, 0]

while True:
    now = time.ticks_ms()
    if time.ticks_diff(now, last_switch) > 3000:
        mode = (mode + 1) % len(modes)
        last_switch = now
        print("mode:", modes[mode])

    img = sensor.snapshot()

    try:
        if modes[mode] == "mean":
            img.mean(1)
        elif modes[mode] == "median":
            img.median(1)
        elif modes[mode] == "mode":
            img.mode(1)
        elif modes[mode] == "midpoint":
            img.midpoint(1)
        elif modes[mode] == "morph":
            img.morph(1, kernel)
        elif modes[mode] == "gaussian":
            img.gaussian(1)
        elif modes[mode] == "laplacian":
            img.laplacian(1)
        elif modes[mode] == "bilateral":
            img.bilateral(1, color_sigma=0.1, space_sigma=1)
        elif modes[mode] == "binary":
            img = img.to_grayscale()
            img.binary([(80, 255)])
        elif modes[mode] == "negate":
            img.negate()
    except Exception as e:
        print("failed:", modes[mode], e)

    color = 255 if img.format() == sensor.GRAYSCALE else (255, 255, 255)
    img.draw_string(10, 10, modes[mode], color=color, scale=2)

    img.flush()
    time.sleep_ms(30)

```

#### 2.3.2 Color Tracking

```python
import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1000)


thresholds = [(29, 66, 54, 88, 26, 93)]

while True:
    img = sensor.snapshot()

    blobs = img.find_blobs(
        thresholds,
        x_stride=1,
        y_stride=1,
        pixels_threshold=20,
        area_threshold=20,
        merge=True,
        margin=10
    )

    if blobs:
        b = max(blobs, key=lambda x: x.pixels())
        img.draw_rectangle(b.rect(), color=(0, 255, 0), thickness=5)
        img.draw_cross(b.cx(), b.cy(), color=(0, 255, 0), thickness=2)

    img.flush()
    time.sleep_ms(20)

```

#### 2.3.3 Frame Difference

```python
import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1000)

print("save background in 2 seconds...")
time.sleep_ms(2000)

bg = sensor.snapshot().copy()
bg.flush()
print("background saved")

while True:
    img = sensor.snapshot()
    img.difference(bg)

    stats = img.get_histogram().get_percentile(0.99)
    print("diff l=", stats.l_value())

    img.flush()
    time.sleep_ms(100)

```

### 2.4 Barcodes API Test

```python
import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1000)

while True:
    img = sensor.snapshot()
    codes = img.find_qrcodes()

    for code in codes:
        img.draw_rectangle(code.rect(), color=(255, 0, 0), thickness=2)
        print("payload:", code.payload())

    img.flush()
    time.sleep_ms(20)

```

```python
import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.GRAYSCALE)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1000)

while True:
    img = sensor.snapshot()
    codes = img.find_barcodes()

    for code in codes:
        img.draw_rectangle(code.rect(), color=255, thickness=2)
        print("payload:", code.payload(), "type:", code.type())

    img.flush()
    time.sleep_ms(20)

```

### 2.5 Feature Detection

> Note: Only Edge is currently supported.

```python
import sensor
import image
import time

sensor.set_pixformat(sensor.GRAYSCALE)
sensor.set_framesize(sensor.QQVGA)
sensor.skip_frames(time=2000)


while True:
    img = sensor.snapshot()
    img.find_edges(image.EDGE_CANNY, threshold=(50, 80))
    img.flush()
    time.sleep_ms(20)

```

## 3. April-Tags

```python
import sensor
import image
import time

sensor.reset()
sensor.set_pixformat(sensor.GRAYSCALE)
sensor.set_framesize(sensor.QQVGA)
sensor.skip_frames(time=1000)

clock = time.clock()

while True:
    clock.tick()
    img = sensor.snapshot()
    tags = img.find_apriltags(families=image.TAG36H11, pose=False)

    for tag in tags:
        img.draw_rectangle(tag.rect, color=255, thickness=2)
        img.draw_cross(tag.cx, tag.cy, color=255, size=10, thickness=2)
        print("id:", tag.id, "fps:", clock.fps())

    img.flush()
    time.sleep_ms(10)

```

## 4. Wi-Fi

### 4.1 Wi-Fi Image Preview

```python
import sensor
import time
import network
import socket

SSID = "SSID"
KEY = "PSWD"
HOST = "0.0.0.0"
PORT = 8080

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1000)

wlan = network.WLAN(network.STA_IF)
wlan.active(True)

wifi_started = False
wifi_last_log = 0
server = None
client = None
streaming = False
last_frame_ms = 0

def close_client():
    global client, streaming
    if client:
        try:
            client.close()
        except:
            pass
    client = None
    streaming = False

while True:
    now = time.ticks_ms()

    if not wlan.isconnected():
        if not wifi_started:
            wlan.connect(SSID, KEY)
            wifi_started = True

        if time.ticks_diff(now, wifi_last_log) > 1000:
            print("wifi status =", wlan.status())
            wifi_last_log = now

        time.sleep_ms(10)
        continue

    if server is None:
        print("wifi:", wlan.ifconfig())
        print("open: http://%s:%d" % (wlan.ifconfig()[0], PORT))

        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind((HOST, PORT))
        server.listen(1)
        server.settimeout(0.05)

    if client is None:
        try:
            c, addr = server.accept()
            c.settimeout(0.05)
            client = c
            streaming = False
            print("client:", addr)
        except OSError:
            pass

    if (client is not None) and (not streaming):
        try:
            req = client.recv(1024)
            if req:
                client.sendall(
                    "HTTP/1.1 200 OK\r\n"
                    "Server: OpenMV\r\n"
                    "Content-Type: multipart/x-mixed-replace; boundary=openmv\r\n"
                    "Cache-Control: no-cache\r\n"
                    "Pragma: no-cache\r\n\r\n"
                )
                streaming = True
        except OSError:
            pass

    if streaming and (client is not None) and (time.ticks_diff(now, last_frame_ms) >= 120):
        try:
            img = sensor.snapshot()
            jpg = img.to_jpeg(quality=20, copy=True)

            client.sendall(
                "\r\n--openmv\r\n"
                "Content-Type: image/jpeg\r\n"
                "Content-Length: %d\r\n\r\n" % jpg.size()
            )
            client.sendall(jpg)
            last_frame_ms = now
        except OSError as e:
            print("client closed:", e)
            close_client()

    time.sleep_ms(10)
```


## 5. Other

### 5.1 Peripheral Support

#### 5.1.1 LCD Support

```python
import sensor
import time
import display

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1000)

lcd = display.ESP32Display(backlight=100)

while True:
    img = sensor.snapshot()
    lcd.write(img)
    img.flush()
    time.sleep_ms(20)

```
