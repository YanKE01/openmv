# ESP32-P4 Sensor Test Scripts

本文档整理当前 `ESP32_GENERIC_P4` 最小 `sensor` 接口的 Python 测试脚本，用于验证：

- `sensor` 模块可导入
- `sensor.reset()` 正常
- `sensor.snapshot()` 可抓图
- OpenMV IDE 预览正常
- `hmirror` / `vflip` 控制可用
- 标准 `Image` 最小模式中的 `to_grayscale()` / `draw_image()` / `find_edges()` 可用

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

当前 `sensor.snapshot()` 返回的是标准 `py_image` 的最小模式 `Image` 对象，已经额外支持：

- `img.flush()`
- `img.to_grayscale(copy=True)`
- `img.binary()`
- `img.find_edges()`
- `img.draw_line()`
- `img.draw_rectangle()`
- `img.draw_circle()`
- `img.draw_cross()`
- `img.draw_string()`
- `img.draw_image()`

当前除上述最小接口外，暂不建议在测试脚本中继续叠加更复杂的 `image` 接口。

当前已经切到标准 `py_image` 的最小模式，旧 `py_image_lite` / `omv_imlib_*_min` / `omv_bar` 路径已删除。

当前已经接入一版最小 draw 能力，可直接在 `sensor.snapshot()` 返回的图像上叠加简单图形，并自动刷新到 IDE 预览，不再需要手动 `img.flush()`。

当前也已经接入一版最小 edge 能力，可直接在 `320x240 RGB565` 图像上运行：

- `image.EDGE_SIMPLE`
- `image.EDGE_CANNY`

当前已确认 `to_grayscale(copy=True)`、`binary()`、`draw_image()`、`find_edges(image.EDGE_CANNY)` 在真机上可正常工作。

需要注意：

- 当前 IDE 预览刷新链路只直接支持 `RGB565`
- `gray.flush()` 对灰度图不会直接更新 IDE 预览
- 如果要在 IDE 中看到灰度图、二值图或边缘图，需要先处理到 `gray`，再用 `img.draw_image(gray, 0, 0)` 贴回 `RGB565` 主图

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

当前 `sensor.snapshot()` 已经会更新预览；这个脚本额外验证返回的 `img.flush()` 仍然可手动刷新预览流。

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

## 脚本 6：移动正方形预览测试

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

## 脚本 7：灰度转换与回贴测试

用于确认标准 `py_image` 最小模式下的 `to_grayscale(copy=True)` 和 `draw_image()` 已经打通。

```python
import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1000)

while True:
    img = sensor.snapshot()
    gray = img.to_grayscale(copy=True)
    img.draw_image(gray, 0, 0)
    time.sleep_ms(80)
```

预期：

- IDE 预览显示整幅灰度图
- 不再出现左上角局部黑白闪动
- `gray.width() == 320`、`gray.height() == 240`、`gray.size() == 76800`

## 脚本 8：Canny 边缘测试

用于确认 `find_edges(image.EDGE_CANNY)` 已经在真机通过。

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
    gray = img.to_grayscale(copy=True)
    gray.find_edges(image.EDGE_CANNY, threshold=(50, 80))
    img.draw_image(gray, 0, 0)
    time.sleep_ms(80)
```

预期：

- IDE 预览显示整幅边缘图
- `EDGE_CANNY` 不报错
- `draw_image()` 回贴后的图像稳定，不出现局部异常块

## 脚本 9：灰度二值化测试

用于确认 `binary()` 已经在当前最小模式下可用，并且结果能通过 `draw_image()` 正确显示到 IDE 预览。

```python
import sensor
import time

sensor.reset()
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.skip_frames(time=1000)

while True:
    img = sensor.snapshot()
    gray = img.to_grayscale(copy=True)
    gray.binary([(120, 255)])
    img.draw_image(gray, 0, 0)
    time.sleep_ms(30)
```

预期：

- IDE 预览显示整幅二值图
- `binary()` 不报错
- 预览不会停留在原始彩色图

如果整体偏黑或偏白，可以尝试调整阈值：

- `[(40, 255)]`
- `[(80, 255)]`
- `[(0, 100)]`

当前不建议直接对 `RGB565` 图像使用 `img.binary([(120, 255)])` 这类灰度阈值脚本，因为画面会很容易看起来全黑，且不利于确认预期行为。

## 已修复问题

本轮 bring-up 中，`to_grayscale(copy=True)` 一度返回异常尺寸：

- `gray.width() == 127`
- `gray.height() == 127`
- `gray.size() == 16129`

根因不是 `draw_image()`，也不是 `py_image` 对象封装，而是 [fmath.h](/home/yanke/project/openmv/lib/imlib/fmath.h) 在非 ARM 路径下的 `fast_floorf()` / `fast_ceilf()` / `fast_roundf()` fallback 使用 `IQmathLib`，在 ESP32-P4 当前环境下把 `320.0` / `240.0` 之类尺寸错误收敛到 `127`。修正为标准 `floorf()` / `ceilf()` / `roundf()` 后：

- `to_grayscale(copy=True)` 恢复正常
- `draw_image()` 恢复正常
- `find_edges(image.EDGE_CANNY)` 也随之恢复正常
- `binary()` 也已在最小模式下接通并通过真机验证
- `img.draw_rectangle()` 不报错

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
6. 如果需要验证最小 draw，再跑“移动正方形预览测试”
7. 如果需要验证 edge，再跑“边缘检测叠加测试”

## 失败现象记录建议

如果测试失败，建议记录以下信息：

- 是否能导入 `sensor`
- 失败发生在 `reset`、`skip_frames` 还是 `snapshot`
- IDE 是否仍能看到后台连续预览
- 串口/REPL 是否出现 `camera init failed`、`snapshot failed`
- 当前板型、传感器型号、接线版本
