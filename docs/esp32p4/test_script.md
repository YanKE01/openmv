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

当前暂不建议在测试脚本中使用复杂 image 算法接口。

当前已经额外接入一版最小 `img.find_barcodes()`，当前直接基于 `sensor.snapshot()` 返回并写入主 framebuffer 的 `320x240` 图像做识别。

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

## 建议测试顺序

1. 先跑“基础初始化”
2. 再跑“单帧抓图”
3. 再跑“连续抓图”
4. 再跑“镜像翻转测试”
5. 最后跑“预览刷新测试”
6. 如果需要验证条码，再跑“CODE128 条码识别”

## 失败现象记录建议

如果测试失败，建议记录以下信息：

- 是否能导入 `sensor`
- 失败发生在 `reset`、`skip_frames` 还是 `snapshot`
- IDE 是否仍能看到后台连续预览
- 串口/REPL 是否出现 `camera init failed`、`snapshot failed`
- 当前板型、传感器型号、接线版本
