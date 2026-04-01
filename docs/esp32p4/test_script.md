# ESP32-P4 Sensor Test Scripts

本文档整理当前 `ESP32_GENERIC_P4` 最小 `sensor` 接口的 Python 测试脚本，用于验证：

- `sensor` 模块可导入
- `sensor.reset()` 正常
- `sensor.snapshot()` 可抓图
- OpenMV IDE 预览正常
- `hmirror` / `vflip` 控制可用

## 当前限制

当前 `sensor` 接口仍是 bring-up 阶段的最小实现，仅支持：

- `RGB565`
- `QVGA`
- `sensor.reset()`
- `sensor.set_pixformat()`
- `sensor.set_framesize()`
- `sensor.skip_frames()`
- `sensor.snapshot()`
- `sensor.get_id()`
- `sensor.set_hmirror()` / `sensor.get_hmirror()`
- `sensor.set_vflip()` / `sensor.get_vflip()`
- `sensor.width()` / `sensor.height()`

当前 `sensor.snapshot()` 返回的最小 `Image` 对象，已经额外支持：

- `img.binary()`
- `img.invert()` / `img.negate()`
- `img.mean()`
- `img.median()`
- `img.gaussian()`
- `img.flush()`
- `img.find_barcodes()`
- `img.find_blobs()`
- `img.find_circles()`
- `img.find_edges()`
- `img.find_rects()`
- `img.draw_line()`
- `img.draw_rectangle()`
- `img.draw_circle()`
- `img.draw_cross()`
- `img.draw_string()`

当前除上述最小算法外，暂不建议在测试脚本中继续叠加更复杂的 `image` 接口。

当前已经额外接入一版最小 `img.find_barcodes()`，当前直接基于 `sensor.snapshot()` 返回并写入主 framebuffer 的 `320x240` 图像做识别。

当前也已经接入一版最小 draw 能力，可直接在 `sensor.snapshot()` 返回的图像上叠加简单图形，并刷新到 IDE 预览。

当前也已经接入一版最小 edge 能力，可直接在 `320x240 RGB565` 图像上运行：

- `image.EDGE_SIMPLE`
- `image.EDGE_CANNY`

当前也已经接入一版最小 filter 能力，可直接在 `320x240 RGB565` 图像上运行：

- `img.binary()`
- `img.invert()` / `img.negate()`
- `img.mean()`
- `img.median()`
- `img.gaussian()`

## 脚本 1：基础初始化

用于确认 `sensor` 模块可导入、初始化正常、基础参数可读。

```python
import sensor

print("sensor module ok")

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=500)

print("sensor id:", hex(sensor.get_id()))
print("width:", sensor.width())
print("height:", sensor.height())
print("hmirror:", sensor.get_hmirror())
print("vflip:", sensor.get_vflip())
```

预期：

- 脚本正常执行
- 不抛异常
- 能打印出 sensor id / width / height
- `width=320`、`height=240`

## 脚本 2：单帧抓图

用于确认 `sensor.snapshot()` 可返回图像对象，并更新 IDE 预览。

```python
import sensor

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=500)

img = sensor.snapshot()

print("snapshot ok")
print("img width:", img.width())
print("img height:", img.height())
print("img format:", img.format())
print("img size:", img.size())
```

预期：

- `sensor.snapshot()` 不报错
- IDE 可看到当前帧
- 返回对象支持 `width()` / `height()` / `format()` / `size()`

## 脚本 3：连续抓图

用于确认连续抓图稳定，IDE 预览持续更新。

```python
import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=500)

start = time.ticks_ms()
frames = 0

while time.ticks_diff(time.ticks_ms(), start) < 5000:
    img = sensor.snapshot()
    frames += 1

print("frames:", frames)
```

预期：

- 5 秒内可连续抓图
- IDE 预览持续刷新
- 无异常、无明显卡死

## 脚本 4：镜像翻转测试

用于确认 `hmirror` / `vflip` 控制链路正常。

```python
import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=500)

print("default:", sensor.get_hmirror(), sensor.get_vflip())

sensor.set_hmirror(True)
sensor.set_vflip(False)
sensor.skip_frames(time=300)
sensor.snapshot()
print("hmirror only:", sensor.get_hmirror(), sensor.get_vflip())
time.sleep_ms(1000)

sensor.set_hmirror(False)
sensor.set_vflip(True)
sensor.skip_frames(time=300)
sensor.snapshot()
print("vflip only:", sensor.get_hmirror(), sensor.get_vflip())
time.sleep_ms(1000)

sensor.set_hmirror(True)
sensor.set_vflip(True)
sensor.skip_frames(time=300)
sensor.snapshot()
print("both:", sensor.get_hmirror(), sensor.get_vflip())
time.sleep_ms(1000)

sensor.set_hmirror(False)
sensor.set_vflip(False)
sensor.skip_frames(time=300)
sensor.snapshot()
print("restore:", sensor.get_hmirror(), sensor.get_vflip())
```

预期：

- 不同镜像/翻转组合下图像方向发生变化
- `get_hmirror()` / `get_vflip()` 返回值与设置一致

## 脚本 5：预览刷新测试

当前 `sensor.snapshot()` 已经会更新预览；这个脚本额外验证返回的 `img.flush()` 也能刷新预览流。

```python
import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=500)

img = sensor.snapshot()
print("first snapshot done")
time.sleep_ms(1000)

img.flush()
print("flush done")
```

预期：

- `snapshot()` 正常
- `img.flush()` 不报错
- IDE 预览可刷新

## 脚本 6：CODE128 条码识别

用于确认 `img.find_barcodes()` 可调用，并能识别拍摄到的 `CODE128` 条码。

```python
import image
import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1500)

print("barcode test start")
print("hold a CODE128 barcode in front of the camera")

last_payload = None
start = time.ticks_ms()

while time.ticks_diff(time.ticks_ms(), start) < 10000:
    img = sensor.snapshot()
    codes = img.find_barcodes()

    if not codes:
        print("no barcode")
        time.sleep_ms(300)
        continue

    for code in codes:
        rect = code.rect()
        payload = code.payload()
        code_type = code.type()
        quality = code.quality()

        print("type:", code_type, "payload:", payload, "rect:", rect, "quality:", quality)

        if code_type == image.CODE128:
            if payload != last_payload:
                print("CODE128 detected:", payload)
                last_payload = payload

    time.sleep_ms(200)

print("barcode test done")
```

预期：

- `img.find_barcodes()` 不报错
- 当镜头对准 `CODE128` 条码时，能打印出 `CODE128 detected: ...`
- 同时会打印 `rect` 和 `quality`

如果你只想做一次单帧验证，也可以用下面这个更短的脚本：

```python
import image
import sensor

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1500)

img = sensor.snapshot()
codes = img.find_barcodes()

print("codes:", len(codes))
for code in codes:
    print("type:", code.type(), "payload:", code.payload(), "rect:", code.rect())
    if code.type() == image.CODE128:
        print("CODE128 ok:", code.payload())
```

## 脚本 7：移动正方形预览测试

用于确认最小 draw 能力可用，并且绘制结果能持续刷新到 IDE 预览。

```python
import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1000)

x = 0
y = 80
size = 40
dx = 4

while True:
    img = sensor.snapshot()
    img.draw_rectangle((x, y, size, size), color=(255, 0, 0), thickness=2)

    x += dx
    if x <= 0 or x >= (320 - size):
        dx = -dx

    time.sleep_ms(30)
```

预期：

- IDE 预览中能看到一个左右移动的红色正方形
- 预览持续刷新，无明显卡死
- `img.draw_rectangle()` 不报错

如果你想验证实心填充，可以把绘制那一行改成：

```python
img.draw_rectangle((x, y, size, size), color=(255, 0, 0), fill=True)
```

如果你想顺手验证 `draw_cross()`，也可以在循环里再加一行：

```python
img.draw_cross((x + size // 2, y + size // 2), color=(255, 255, 0), size=8, thickness=2)
```

## 脚本 8：EDGE_SIMPLE 连续预览测试

用于确认 `img.find_edges(image.EDGE_SIMPLE)` 可连续运行，并把边缘结果持续刷新到 IDE 预览。

```python
import image
import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1000)

while True:
    img = sensor.snapshot()
    img.find_edges(image.EDGE_SIMPLE, threshold=(40, 80))
    time.sleep_ms(30)
```

预期：

- IDE 预览中能持续看到二值化边缘结果
- `img.find_edges()` 不报错
- 连续运行无明显卡死

如果边缘太少，可以把阈值调低，例如：

```python
img.find_edges(image.EDGE_SIMPLE, threshold=(20, 40))
```

## 脚本 9：EDGE_CANNY 连续预览测试

用于确认 `img.find_edges(image.EDGE_CANNY)` 可连续运行，并在 IDE 预览中看到较干净的边缘结果。

```python
import image
import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1000)

while True:
    img = sensor.snapshot()
    img.find_edges(image.EDGE_CANNY, threshold=(50, 100))
    time.sleep_ms(30)
```

预期：

- IDE 预览中能持续看到 Canny 边缘结果
- `img.find_edges()` 不报错
- 连续运行无明显卡死

如果边缘太少，可以把阈值调低，例如：

```python
img.find_edges(image.EDGE_CANNY, threshold=(30, 60))
```

## 脚本 10：色块识别连续预览测试

用于确认 `img.find_blobs()` 可运行，并能在 IDE 预览中看到色块框选结果。

下面这个示例先用一组偏红色的 LAB 阈值做测试，阈值需要按现场光照和目标颜色微调。

```python
import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1500)

thresholds = [(19, 51, 27, 62, -15, 64)]

while True:
    img = sensor.snapshot()
    blobs = img.find_blobs(thresholds, pixels_threshold=80, area_threshold=80)

    for blob in blobs:
        img.draw_rectangle(blob.rect(), color=(255, 0, 0), thickness=2)
        img.draw_cross((blob.cx(), blob.cy()), color=(0, 255, 0), size=6, thickness=2)
        print("blob:", blob.rect(), "pixels:", blob.pixels(), "center:", blob.cx(), blob.cy())

    time.sleep_ms(50)
```

预期：

- `img.find_blobs()` 不报错
- 当画面中出现接近阈值的色块时，IDE 预览中能看到框和中心十字
- REPL 会打印 `rect / pixels / center`

如果现场颜色和阈值不匹配，可以先试更简单的亮度阈值：

```python
thresholds = [(180, 255)]
```

## 脚本 11：圆形识别连续预览测试

用于确认 `img.find_circles()` 可运行，并能把检测到的圆叠加到 IDE 预览。

```python
import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1500)

while True:
    img = sensor.snapshot()
    circles = img.find_circles(threshold=2500, x_margin=10, y_margin=10,
                               r_margin=10, r_min=10, r_max=80, r_step=2)

    for c in circles:
        img.draw_circle(c.circle(), color=(255, 0, 0), thickness=2)
        print("circle:", c.circle(), "magnitude:", c.magnitude())

    time.sleep_ms(50)
```

预期：

- `img.find_circles()` 不报错
- 面对圆形目标时，IDE 预览中能看到圆形叠加
- REPL 会打印圆心、半径和 `magnitude`

如果误检较多，可以提高 `threshold`；如果检不出来，可以先降到 `1500` 左右试。

## 脚本 12：矩形识别连续预览测试

用于确认 `img.find_rects()` 可运行，并能在 IDE 预览中看到矩形四边叠加。

```python
import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1500)

while True:
    img = sensor.snapshot()
    rects = img.find_rects(threshold=1200)

    for r in rects:
        corners = r.corners()
        img.draw_line((corners[0][0], corners[0][1], corners[1][0], corners[1][1]), color=(255, 0, 0), thickness=2)
        img.draw_line((corners[1][0], corners[1][1], corners[2][0], corners[2][1]), color=(255, 0, 0), thickness=2)
        img.draw_line((corners[2][0], corners[2][1], corners[3][0], corners[3][1]), color=(255, 0, 0), thickness=2)
        img.draw_line((corners[3][0], corners[3][1], corners[0][0], corners[0][1]), color=(255, 0, 0), thickness=2)
        print("rect:", r.rect(), "magnitude:", r.magnitude())

    time.sleep_ms(50)
```

预期：

- `img.find_rects()` 不报错
- 面对边缘较清晰的矩形目标时，IDE 预览中能看到矩形轮廓
- REPL 会打印矩形包围框和 `magnitude`

如果结果太少，可以把 `threshold` 降低一些，例如 `800`。

## 脚本 13：二值化连续预览测试

用于确认 `img.binary()` 可连续运行，并把二值化结果持续刷新到 IDE 预览。

```python
import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1000)

while True:
    img = sensor.snapshot()
    img.binary([(120, 255)])
    time.sleep_ms(30)
```

预期：

- `img.binary()` 不报错
- IDE 预览中能持续看到黑白二值化结果
- 连续运行无明显卡死

如果画面整体太黑或太白，可以调整阈值，例如：

```python
img.binary([(80, 255)])
```

如果想测试反相阈值，也可以改成：

```python
img.binary([(120, 255)], invert=True)
```

## 脚本 14：反相连续预览测试

用于确认 `img.invert()` / `img.negate()` 可连续运行，并把反相后的图像持续刷新到 IDE 预览。

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
    time.sleep_ms(30)
```

预期：

- `img.invert()` 不报错
- IDE 预览中颜色明显反相
- 连续运行无明显卡死

如果想验证别名接口，也可以改成：

```python
img.negate()
```

## 脚本 15：均值滤波连续预览测试

用于确认 `img.mean()` 可连续运行，并在 IDE 预览中看到平滑后的图像。

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
    time.sleep_ms(30)
```

预期：

- `img.mean()` 不报错
- IDE 预览中画面边缘和纹理有轻微平滑效果
- 连续运行无明显卡死

如果想增强效果，可以试：

```python
img.mean(2)
```

如果想测试阈值模式，也可以试：

```python
img.mean(1, threshold=True, offset=5)
```

## 脚本 16：中值滤波连续预览测试

用于确认 `img.median()` 可连续运行，并在 IDE 预览中看到噪点抑制效果。

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
    time.sleep_ms(30)
```

预期：

- `img.median()` 不报错
- IDE 预览中局部噪点和细碎纹理有所减弱
- 连续运行无明显卡死

如果想增强效果，可以试：

```python
img.median(2, percentile=0.5)
```

## 脚本 17：高斯滤波连续预览测试

用于确认 `img.gaussian()` 可连续运行，并在 IDE 预览中看到平滑后的图像。

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
    time.sleep_ms(30)
```

预期：

- `img.gaussian()` 不报错
- IDE 预览中画面有明显平滑效果
- 连续运行无明显卡死

如果想测试锐化路径，也可以试：

```python
img.gaussian(1, unsharp=True)
```

## 建议测试顺序

1. 先跑“基础初始化”
2. 再跑“单帧抓图”
3. 再跑“连续抓图”
4. 再跑“镜像翻转测试”
5. 最后跑“预览刷新测试”
6. 如果需要验证条码，再跑“CODE128 条码识别”
7. 如果需要验证最小 draw，再跑“移动正方形预览测试”
8. 如果需要验证 edge，再跑“EDGE_SIMPLE 连续预览测试”
9. 再跑“EDGE_CANNY 连续预览测试”
10. 如果需要验证色块识别，再跑“色块识别连续预览测试”
11. 如果需要验证形状识别，再跑“圆形识别连续预览测试”
12. 再跑“矩形识别连续预览测试”
13. 如果需要验证 filter，再跑“二值化连续预览测试”
14. 再跑“反相连续预览测试”
15. 再跑“均值滤波连续预览测试”
16. 再跑“中值滤波连续预览测试”
17. 最后跑“高斯滤波连续预览测试”

## 失败现象记录建议

如果测试失败，建议记录以下信息：

- 是否能导入 `sensor`
- 失败发生在 `reset`、`skip_frames` 还是 `snapshot`
- IDE 是否仍能看到后台连续预览
- 串口/REPL 是否出现 `camera init failed`、`snapshot failed`
- 当前板型、传感器型号、接线版本
