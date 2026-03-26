# ESP32P4 QVGA Test Notes

当前主分辨率固定为：

- `sensor.RGB565`
- `sensor.QVGA`
- 图像尺寸：`320x240`

当前推荐在 OpenMV IDE 中开启：

- `动态帧获取`
- `组合轮询`

说明：

- 当前 `QVGA` 预览在这组设置下最稳定。
- 若使用默认拉流策略，可能会出现周期性掉帧或短时 `0fps`。

## 1. 基本拍照验证

```python
import sensor

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)

img = sensor.snapshot()
print(img.width(), img.height(), img.size())
```

预期输出：

```python
320 240 153600
```

## 2. 画图验证

```python
import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1000)

x = 0
dx = 8

while True:
    img = sensor.snapshot()
    img.draw_line((0, 0, 319, 239), color=(255, 0, 0), thickness=2)
    img.draw_line((0, 239, 319, 0), color=(0, 255, 0), thickness=2)
    img.draw_rectangle((x, 70, 80, 60), color=(255, 255, 0), thickness=2)
    img.draw_circle(160, 120, 30, color=(0, 0, 255), thickness=2)
    img.draw_cross(160, 120, color=(255, 255, 255), size=20, thickness=2)
    img.flush()

    x += dx
    if x <= 0 or x >= 240:
        dx = -dx

    time.sleep_ms(30)

```

说明：

- `img.flush()` 用于在一轮绘制完成后统一刷新预览。
- 当前如果每次 `draw_*` 都立刻刷新，FPS 会明显下降。

## 3. 二值化验证

```python
import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1000)

while True:
    img = sensor.snapshot()
    img.binary((80, 255))
    img.flush()
    time.sleep_ms(30)
```

反相二值化：

```python
import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1000)

while True:
    img = sensor.snapshot()
    img.binary((80, 255), invert=True)
    img.flush()
    time.sleep_ms(30)
```

## 4. 反相验证

```python
import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1000)

while True:
    img = sensor.snapshot()
    img.invert()
    img.flush()
    time.sleep_ms(30)
```

或：

```python
img.negate()
```

## 5. Filter 验证

当前第一批已接入：

- `img.mean()`
- `img.median()`
- `img.gaussian()`

当前边界：

- 仅支持 `RGB565`
- 当前不支持 `mask`
- 当前是最小实现，先用于功能验证

### mean

```python
import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1000)

while True:
    img = sensor.snapshot()
    img.mean(1)
    img.flush()
    time.sleep_ms(30)
```

### median

```python
import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1000)

while True:
    img = sensor.snapshot()
    img.median(1, percentile=0.5)
    img.flush()
    time.sleep_ms(30)
```

### gaussian

```python
import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1000)

while True:
    img = sensor.snapshot()
    img.gaussian(1)
    img.flush()
    time.sleep_ms(30)
```

### gaussian + unsharp

```python
import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1000)

while True:
    img = sensor.snapshot()
    img.gaussian(1, unsharp=True)
    img.flush()
    time.sleep_ms(30)
```

## 6. 翻转控制验证

```python
import sensor

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)

print(hex(sensor.get_id()))
print(sensor.get_hmirror(), sensor.get_vflip())

sensor.set_hmirror(True)
sensor.set_vflip(True)

print(sensor.get_hmirror(), sensor.get_vflip())
```

当前 `sensor.get_id()` 预期返回：

```python
0xcb3a
```

## 7. 寻找色块验证

当前 `find_blobs()` 先提供最小可用版本，支持：

- 亮度阈值：`(lo, hi)`
- LAB 阈值：`(l_lo, l_hi, a_lo, a_hi, b_lo, b_hi)`
- `pixels_threshold`
- `area_threshold`
- `invert`
- `roi`

当前暂不支持：

- `merge=True`
- `margin != 0`
- `x_stride/y_stride != 1`

亮度阈值示例：

```python
import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1000)

thresholds = [(180, 255)]

while True:
    img = sensor.snapshot()
    blobs = img.find_blobs(thresholds, pixels_threshold=100, area_threshold=100)

    for b in blobs:
        img.draw_rectangle(b.rect(), color=(255, 0, 0), thickness=2)
        img.draw_cross(b.cx(), b.cy(), color=(0, 255, 0), size=10, thickness=2)

    img.flush()
    time.sleep_ms(30)
```

LAB 阈值示例：

```python
import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1000)

thresholds = [(19, 50, 32, 65, -1, 64)]

while True:
    img = sensor.snapshot()
    blobs = img.find_blobs(thresholds, pixels_threshold=100, area_threshold=100)

    for b in blobs:
        img.draw_rectangle(b.rect(), color=(255, 0, 0), thickness=2)
        img.draw_cross(b.cx(), b.cy(), color=(0, 255, 0), size=10, thickness=2)
        print(b)

    img.flush()
    time.sleep_ms(30)
```

当前 blob 对象可用接口：

- `b.rect()`
- `b.pixels()`
- `b.cx()`
- `b.cy()`
- `b.code()`
- `b.count()`

## 8. 当前扫码识别状态

- `find_barcodes()` / `find_qrcodes()` 当前均未并入主线。
- 原因是当前 `ESP32P4` 侧 `zbar/qrcode` 依赖收敛和识别效果都还不稳定。
- 后续会单独整理，不影响当前 `QVGA + 画图 + 二值化 + blobs/rects/circles` 主线。

## 8. 矩形检测验证

当前 `find_rects()` 是最小实现，先做 `QVGA RGB565` 下的基础检测，适合明显边缘的矩形目标。

```python
import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1000)

while True:
    img = sensor.snapshot()
    rects = img.find_rects(threshold=3000)

    for r in rects:
        print(r)
        img.draw_rectangle(r.rect(), color=(255, 0, 0), thickness=2)

    img.flush()
    time.sleep_ms(30)
```

当前 rect 对象可用接口：

- `r.rect()`
- `r.corners()`
- `r.magnitude()`

## 9. 圆检测验证

当前 `find_circles()` 也是最小实现，适合圆边界清楚、背景比较简单的目标。

```python
import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1000)

while True:
    img = sensor.snapshot()
    circles = img.find_circles(threshold=2500, x_margin=10, y_margin=10, r_margin=10,
                               r_min=10, r_max=80, r_step=2)

    for c in circles:
        print(c)
        x, y, r = c.circle()
        img.draw_circle(x, y, r, color=(0, 255, 0), thickness=2)

    img.flush()
    time.sleep_ms(30)
```

当前 circle 对象可用接口：

- `c.circle()`
- `c.magnitude()`

## 10. 边缘检测验证

当前 `find_edges()` 支持：

- `image.EDGE_SIMPLE`
- `image.EDGE_CANNY`
- `threshold=(low, high)`
- `roi=(x, y, w, h)`

### EDGE_SIMPLE

```python
import sensor
import image
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1000)

while True:
    img = sensor.snapshot()
    img.find_edges(image.EDGE_SIMPLE, threshold=(50, 80))
    img.flush()
    time.sleep_ms(30)
```

### EDGE_CANNY

```python
import sensor
import image
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1000)

while True:
    img = sensor.snapshot()
    img.find_edges(image.EDGE_CANNY, threshold=(50, 80))
    img.flush()
    time.sleep_ms(30)
```

### 边缘检测 + 画框验证

```python
import sensor
import image
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1000)

while True:
    img = sensor.snapshot()
    img.find_edges(image.EDGE_CANNY, threshold=(50, 80))
    img.draw_rectangle((80, 60, 160, 120), color=(255, 0, 0), thickness=2)
    img.flush()
    time.sleep_ms(30)
```

### 当前性能参考

- `img.find_edges(image.EDGE_SIMPLE, ...)` 约 `6fps`
- `img.find_edges(image.EDGE_CANNY, ...)` 约 `5fps`
- 纯预览约 `30fps`

## 11. 当前已验证可用接口

- `sensor.reset()`
- `sensor.set_pixformat(sensor.RGB565)`
- `sensor.set_framesize(sensor.QVGA)`
- `sensor.snapshot()`
- `sensor.get_id()`
- `sensor.skip_frames()`
- `sensor.set_hmirror()/get_hmirror()`
- `sensor.set_vflip()/get_vflip()`
- `img.get_pixel()`
- `img.set_pixel()`
- `img.draw_line()`
- `img.draw_rectangle()`
- `img.draw_circle()`
- `img.draw_cross()`
- `img.binary()`
- `img.find_blobs()`
- `img.find_rects()`
- `img.find_circles()`
- `img.find_edges()`
- `img.invert()`
- `img.negate()`
- `img.flush()`

## 12. 当前说明

- 当前主图像分辨率固定为 `320x240`
- 底层相机输入仍为 `640x480 RGB565`
- 固件内部使用 `PPA` 缩放到 `320x240 RGB565`
- IDE 预览输出当前为 `JPEG`
