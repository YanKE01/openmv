# Phase 3 — CSI 摄像头

**状态：进行中（真实采图、标准 `py_image` 最小模式和 IDE 预览已通过）**

## 目标

- 在 `ports/esp32` 实现真实的 CSI 摄像头驱动
- 完成稳定采图
- OpenMV IDE 能看到连续预览帧
- 接入最小 `sensor` 模块，支持基础 Python 抓图测试
- `image` 算法先按最小子集逐步补齐

## 前置条件

- Phase 2（IDE 链路）完成

## 当前结论

当前 ESP32-P4 已经可以向 OpenMV IDE 正常输出预览帧。

当前已经接入一版 **ESP32 专用的最小 `sensor` 模块**，可以通过 Python 调用 `sensor.reset()`、`sensor.snapshot()` 等接口完成基础抓图。

当前 `snapshot()` 返回值已不再是 `py_image_lite`，而是标准 `modules/py_image.c` 提供的 `Image` 对象；ESP32 侧通过最小模式裁剪方法表，只先开放试水所需的能力。

当前脚本层已确认可调用：

- `img.find_edges()`
- `img.draw_line()` / `draw_rectangle()` / `draw_circle()` / `draw_cross()` / `draw_string()`
- `img.to_grayscale(copy=True)`
- `img.draw_image()`

当前真机已经确认通过：

- `draw_*` 预览叠加
- `to_grayscale(copy=True)`
- `draw_image()`
- `find_edges(image.EDGE_CANNY)`

但当前仍属于 bring-up 阶段的临时收口方案，还不是完整的 OpenMV `sensor + image` 能力集。也就是说：

- IDE 预览已正常
- 真实 CSI 采集已打通
- 最小 `sensor` 模块已接入
- 基础 `sensor` Python 测试已通过
- 标准 `py_image` 已接入，但当前只开放 `draw_*`、`find_edges()` 和基础 `Image` 接口
- `to_grayscale(copy=True)` 和 `draw_image()` 已经在当前最小模式下验证可用
- `draw_*` 修改后的图像现在会主动刷新到 IDE 预览
- 当前 `sensor` 路径仍直接复用 `omv_camera.c`，不是完整 `omv_csi` port 方案
- 旧 `py_image_lite`、`omv_imlib_*_min` 和 `omv_bar` 已删除，不再作为保底路径
- 完整 `image` 能力仍未补齐，当前仍是最小 bring-up 子集

## 本轮关键修复

在标准 `py_image` 最小模式接入后，`to_grayscale(copy=True)` 一度返回错误尺寸：

- `gray.width() == 127`
- `gray.height() == 127`
- `gray.size() == 16129`

这最初看起来像是 `draw_image()` 或 `Image` 封装链问题，但最终定位到 [fmath.h](/home/yanke/project/openmv/lib/imlib/fmath.h) 的非 ARM fallback：

- `fast_floorf()`
- `fast_ceilf()`
- `fast_roundf()`

原先使用 `IQmathLib` 的 `_IQint(_IQ(x))` 路径，在 ESP32-P4 当前环境下会把 `320.0f` / `240.0f` 之类尺寸计算错误压到 `127` 附近，导致 `py_image_to()` 内部构造的 `dst_img.w/h` 在进入算法前就已经错误。

修复方式是把非 ARM 路径改为标准：

- `floorf()`
- `ceilf()`
- `roundf()`

修复后，以下链路均已恢复并通过真机验证：

- `to_grayscale(copy=True)`
- `draw_image()`
- `find_edges(image.EDGE_CANNY)`

这个修复不是 ESP32 特有 workaround，而是 RISC-V / 非 ARM 路径下的通用 fast-math 兼容性修复。

## 当前技术路线

### 1. 启动链路

在 `ports/esp32/main.c` 中，启动时会依次初始化：

- `framebuffer_init0()`
- `omv_esp32_camera_init0()`
- `omv_esp32_test_preview_init0()`
- `omv_esp32_camera_init()`

这样系统启动后就会建立摄像头采集和预览发布的基础运行环境。

需要注意的是，当前在 `OMV_PY_IMAGE_ESP32_MINIMAL` 模式下，`omv_esp32_test_preview_init0()` 会直接返回，不再启动后台测试预览任务。这样做是为了避免后台原始相机流持续覆盖 `draw_*` 后主动推送到 IDE 的 JPEG 预览。

### 2. 摄像头采集链路

当前采集链路实现在 `ports/esp32/omv_camera.c`，主要依赖 ESP-IDF 的视频子系统：

- 通过 `esp_video_init()` 初始化 ESP32-P4 CSI 子系统
- 打开 `ESP_VIDEO_MIPI_CSI_DEVICE_NAME`
- 使用 V4L2 风格接口进行 buffer 申请、`QBUF/DQBUF` 取帧
- 当前像素格式按 `RGB565` 路径工作

当前这层不是 OpenMV 传统的 `omv_csi.c` port 实现，而是先直接走了 ESP-IDF 视频栈，目标是优先把真实图像和 IDE 预览跑通。

当前硬件 bring-up 所需的摄像头参数也已经直接写在 `omv_camera.c` 中，包括：

- SCCB I2C 端口与引脚
- 目标输入/输出分辨率
- 当前 reset / pwdn 配置

对于当前使用的 MIPI 摄像头场景，CSI 连接本身是固定的，不需要像并口 DVP 那样逐根数据线再做单独确认。

当前 `omv_camera.c` 对上层保留的抓图入口是：

- `omv_esp32_camera_capture_rgb565()`：返回经过 PPA 裁剪/缩放后的 `320x240 RGB565`

当前预览、最小 `sensor`，以及现阶段的条码识别都统一基于这一份 `320x240` 图像，优先保证实现简单和运行流畅。

### 3. 图像裁剪与缩放

当前采图后，使用 ESP32-P4 的 PPA 做 crop/scale：

- 输入目标尺寸当前按 `960x540`
- 有效裁剪窗口当前按 `640x480`
- 输出预览尺寸当前固定为 `320x240`

这样做的目的是：

- 降低 bring-up 阶段的带宽和内存压力
- 优先保证 OpenMV IDE 预览链路稳定
- 先验证 CSI + DMA + cache + PPA 整体链路

### 4. IDE 预览发布链路

当前预览发布主链路实现在 `ports/esp32/omv_framebuffer.c` 和 `ports/esp32/py_sensor.c`：

- `sensor.snapshot()` 抓取 `320x240 RGB565` 到主 framebuffer
- `framebuffer_update_preview()` 将 RGB565 编码为 JPEG
- JPEG 写入 `FB_STREAM_ID`
- 通过 `omv_protocol` 的 stream 通道向 OpenMV IDE 推送预览事件

当前 OpenMV IDE 看到的预览帧，主要来源于 `sensor.snapshot()` 以及 `draw_*` / `draw_image()` / `flush()` 触发的主动刷新。后台 `omv_test_preview` 任务仍保留源码，但在当前最小模式下不会启动。

### 5. framebuffer 支撑

当前 `ports/esp32/omv_framebuffer.c` 提供了最小可用 framebuffer 能力：

- 主 framebuffer
- stream framebuffer
- RGB565 -> JPEG 预览更新

这部分已经足够支撑 IDE 预览，但还不是完整的 OpenMV framebuffer 实现。

### 6. Python `sensor` 接口

当前已经在 `ports/esp32/py_sensor.c` 中接入最小 `sensor` 模块，当前支持：

- `sensor.reset()`
- `sensor.set_pixformat(sensor.RGB565)`
- `sensor.set_framesize(sensor.QVGA)`
- `sensor.skip_frames()`
- `sensor.snapshot()`
- `sensor.get_id()`
- `sensor.set_hmirror()` / `sensor.get_hmirror()`
- `sensor.set_vflip()` / `sensor.get_vflip()`
- `sensor.width()` / `sensor.height()`

当前 `snapshot()` 返回的是标准 `py_image` 提供的 `Image` 对象，但 ESP32 当前通过最小模式只开放基础元信息、`flush()`、`to_grayscale(copy=True)`、`draw_*`、`draw_image()` 和 `find_edges()` 这批接口，不代表完整 OpenMV `image` 算法接口已经完成。

## 补充：当前实现涉及的文件与作用

这一节把当前 Phase 3 实际改动到的关键文件串起来，说明每个文件在当前最小 `sensor` + 预览链路中承担什么职责。

### 1. `ports/esp32/micropython.cmake`

这是 ESP32 port 下 OpenMV 自定义代码接入 MicroPython/ESP-IDF 构建的入口。

当前 Phase 3 相关作用：

- 把 `omv_camera.c` 挂进最终固件
- 把 `omv_framebuffer.c` 挂进最终固件
- 把 `omv_test_preview.c` 挂进最终固件
- 把 `py_sensor.c` 挂进最终固件
- 把标准 `modules/py_image.c` 挂进最终固件
- 开启 `OMV_PROTOCOL_HAS_FRAMEBUFFER=1`，让 stream 通道具备 framebuffer 预览能力

可以把它理解为：**Phase 3 新增能力如何进入最终 ESP32 固件** 的总入口。

### 2. `ports/esp32/main.c`

这是 ESP32 OpenMV 运行时主循环入口。

当前 Phase 3 相关作用：

- 启动时初始化 framebuffer
- 启动时初始化摄像头上下文
- 启动时初始化标准 `Image` 支撑
- 启动时初始化测试预览入口（当前最小模式下不启动后台任务）
- 在进入 REPL / IDE 脚本执行前完成摄像头初始化

也就是说，这个文件负责把 Phase 3 的各个部件按正确顺序启动起来。

### 3. `ports/esp32/omv_camera.c`

这是当前真实采图链路的核心文件。

当前作用：

- 调用 `esp_video_init()` 初始化 ESP32-P4 MIPI CSI 视频子系统
- 配置 SCCB/I2C、reset、pwdn 等当前 bring-up 硬件参数
- 打开 `ESP_VIDEO_MIPI_CSI_DEVICE_NAME`
- 使用 V4L2 风格接口申请采集 buffer
- 通过 `VIDIOC_DQBUF/QBUF` 取回原始帧
- 使用 PPA 完成 crop / scale
- 输出 `320x240 RGB565`
- 提供 `hmirror` / `vflip` 控制

对上层暴露的抓图接口是：

- `omv_esp32_camera_capture_rgb565()`：给预览、当前最小 `sensor`，以及现阶段条码识别使用

当前最小 `sensor` 模块和预览刷新链路，底层都复用了这一个抓图入口。

### 4. `ports/esp32/omv_framebuffer.c`

这是当前 ESP32 最小 framebuffer 实现。

当前作用：

- 分配主 framebuffer（`FB_MAINFB_ID`）
- 分配 stream framebuffer（`FB_STREAM_ID`）
- 提供 `framebuffer_get()` / `framebuffer_to_image()` 等基础接口
- 把主 framebuffer 中的 RGB565 图像编码成 JPEG
- 将 JPEG 结果写入 stream framebuffer
- 通过 `framebuffer_update_preview()` 向 IDE 预览链路更新图像

这个文件的定位不是完整 OpenMV framebuffer 实现，而是先满足：

- `sensor.snapshot()` 返回主 framebuffer 图像
- IDE 能收到 JPEG 预览帧

### 5. `ports/esp32/omv_test_preview.c`

这是 Phase 3 bring-up 时保留下来的测试预览任务实现。

当前作用：

- 保留一条独立于 Python 图像栈的连续预览实现
- 便于在需要时单独验证相机抓图和 stream 通道

当前状态：

- 在 `OMV_PY_IMAGE_ESP32_MINIMAL` 模式下不启动
- 原因是它会持续写入 `FB_STREAM_ID`，覆盖 `draw_*` 后主动推送的 JPEG 预览
- 当前 IDE 预览主路径不再依赖它

### 6. `ports/esp32/py_sensor.c`

这是当前最小 `sensor` Python 模块实现。

当前作用：

- 暴露 `sensor.reset()`
- 限定当前支持的 `pixformat` 为 `RGB565`
- 限定当前支持的 `framesize` 为 `QVGA`
- 调用 `omv_esp32_camera_capture_rgb565()` 完成 `sensor.snapshot()`
- 将结果写入主 framebuffer
- 在 `snapshot()` 后触发一次预览更新
- 暴露 `skip_frames()`、`get_id()`、`hmirror/vflip` 等最小控制接口

这个文件的目标不是完整复现 OpenMV 全量 `sensor` 语义，而是先给 Python 层一个可用的抓图入口。

### 7. `modules/py_image.c`

当前 `sensor.snapshot()` 返回值已经切到标准 `modules/py_image.c` 中的 `Image` 对象。

当前作用：

- 复用主 OpenMV `Image` 类型和大部分公共语义
- 通过 `OMV_PY_IMAGE_ESP32_MINIMAL` 在 ESP32 上裁剪方法表
- 当前先开放 `draw_*`、`draw_image()`、`flush()`、`to_grayscale(copy=True)`、`find_edges()` 和基础元信息
- `draw_*` / `draw_image()` 在 ESP32 最小模式下会主动触发 IDE 预览刷新

这样做的目标是避免继续维护 `py_image_lite` 和一批 `omv_imlib_*_min` 分叉实现，后续逐步向标准 `image` 栈收敛。

### 8. `lib/imlib/fmath.h`

这是这轮排查里额外确认的关键公共依赖。

当前作用：

- 为 `py_image` / `imlib` 的尺寸、缩放、坐标等计算提供 `fast_floorf()` / `fast_ceilf()` / `fast_roundf()`

本轮修复：

- 将非 ARM 路径从 `IQmathLib` fallback 改为标准 `floorf()` / `ceilf()` / `roundf()`

影响：

- 修复 `to_grayscale(copy=True)` 输出尺寸错误
- 修复 `draw_image()` 回贴异常
- 降低后续 `draw/filter/blob/stats` 等模块在 ESP32-P4 上继续出现尺寸/坐标异常的风险

### 9. `boards/ESP32_GENERIC_P4/omv_boardconfig.h`

这是当前板级配置头。

当前 Phase 3 相关作用：

- 定义主 framebuffer 大小（`OMV_FB_SIZE`）
- 定义 stream buffer 大小（`OMV_SB_SIZE`）
- 定义 JPEG 质量相关参数

这些配置直接影响当前抓图和预览路径的内存占用与 JPEG 输出空间。

## 补充：当前图像是如何采集和传输的

这一节从数据流的角度，把“相机数据怎么进入 OpenMV，再怎么到 IDE”串起来。

### 1. 从 MIPI 摄像头到 ESP32 视频驱动

当前摄像头通过 ESP32-P4 的 MIPI CSI 接口进入芯片。

运行时：

- `esp_video_init()` 负责初始化视频子系统
- 驱动创建设备节点 `ESP_VIDEO_MIPI_CSI_DEVICE_NAME`
- `omv_camera.c` 通过 `open()` + `ioctl()` 与该设备交互

因此，当前采图不是自己直接写一套底层 CSI/DMA 驱动，而是复用 ESP-IDF 的视频抽象层。

### 2. 从视频驱动到原始帧 buffer

`omv_camera.c` 在初始化时：

- 通过 `VIDIOC_REQBUFS` 申请采集缓冲区
- 通过 `VIDIOC_QUERYBUF` 获取每个 buffer 信息
- 通过 `mmap()` 把驱动 buffer 映射到用户侧地址空间
- 通过 `VIDIOC_QBUF` 先把 buffer 全部放回采集队列

抓图时：

- 通过 `VIDIOC_DQBUF` 取出一帧
- 拿到当前帧的原始图像地址
- 处理完成后再通过 `VIDIOC_QBUF` 归还 buffer

也就是说，当前原始图像首先落在 ESP-IDF 视频驱动管理的采集 buffer 中。

### 3. 从原始帧到 `320x240 RGB565`

当前 `VIDIOC_DQBUF` 取回的一帧，首先是驱动管理的原始输入帧。

当前取出的原始帧不会直接交给 Python 或 IDE，而是先经过一次 PPA 处理：

- 从输入图像中取 `640x480` 的有效区域
- 按配置缩放到 `320x240`
- 输出为 `RGB565`

这样做的结果是：

- Python 层看到的是固定 `QVGA`
- IDE 连续预览也统一使用 `QVGA`
- 带宽、内存和 JPEG 编码压力更可控

### 4. 传给 Python：`sensor.snapshot()` 路径

当 Python 调用：

```python
img = sensor.snapshot()
```

当前内部流程是：

```text
py_sensor.c
  └── omv_esp32_camera_capture_rgb565()
        └── 从 omv_camera.c 抓一帧 320x240 RGB565
  └── 写入 FB_MAINFB_ID
  └── framebuffer_update_preview()
        └── 把主 framebuffer 压缩成 JPEG 预览帧
  └── 返回标准 py_image.c 提供的 Image 对象
```

所以当前 `sensor.snapshot()` 做了两件事：

- 为 Python 返回一张图
- 顺便把当前图像刷新到 IDE 预览流

### 5. 当前图像路径关系

当前系统里保留了两条“可发送到 IDE”的路径，但最小模式下只有一条在工作：

#### 路径 A：Python 抓图/绘制路径

- 目标：让脚本获得一张图像，并把修改结果同步到 IDE
- buffer：`FB_MAINFB_ID` -> `FB_STREAM_ID`
- 编码：RGB565 经 `framebuffer_update_preview()` 压缩为 JPEG
- 触发方式：`sensor.snapshot()`、`img.draw_*()`、`img.draw_image()`、`img.flush()`

#### 路径 B：后台测试预览路径

- 目标：独立验证相机抓图和 stream 通道
- buffer：`FB_STREAM_ID`
- 编码：总是 JPEG
- 触发方式：`omv_test_preview_task`
- 当前状态：`OMV_PY_IMAGE_ESP32_MINIMAL` 下禁用

这样处理的原因很直接：如果后台测试预览任务继续运行，它会持续写入 `FB_STREAM_ID`，覆盖 `draw_*` 后主动推送的 JPEG 帧，导致 IDE 看不到叠加图形。

### 6. 当前实现的边界

当前已经完成的是：

- 真正从 MIPI 摄像头采到图
- 将图像缩放到固定 `320x240 RGB565`
- 让 IDE 能看到 `snapshot()` 和 `draw_*` 主动刷新的预览
- 让 Python 能通过最小 `sensor` 模块抓图
- 已切到标准 `py_image` 最小模式
- `draw_*` / `draw_image()` 修改后可直接反映到 IDE 预览

当前还没有完成的是：

- 标准 `omv_csi` 方案收口
- 完整 sensor probe / sensor id 机制
- 完整 OpenMV `image` 算法接口
- 更多图像算法与更完整的 image 语义

## 任务列表

### 硬件确认

- [x] 确认当前目标开发板与 MIPI CSI 接入方式
- [x] 当前摄像头 bring-up 所需硬件参数已落到 `omv_camera.c`
- [x] 当前 SCCB、reset、pwdn 等配置已在实现中固化
- [ ] 将当前硬编码参数下沉到板级配置

### 驱动实现

- [x] 基于 ESP-IDF 视频栈打通真实 CSI 图像采集
- [x] 打通 OpenMV IDE 预览所需的 JPEG stream 发布
- [x] 打通 PPA 裁剪/缩放链路
- [x] 接入最小 `sensor` 模块
- [x] 接入标准 `py_image` 最小模式
- [x] `draw_*` / `draw_image()` 已接入自动预览刷新
- [x] 最小模式下关闭后台测试预览任务，避免覆盖绘制结果
- [ ] 实现真实 `sensor probe`
- [ ] 接入真实 sensor id，而不是当前占位返回值
- [ ] 实现复位和电源控制的正式板级配置
- [ ] 评估是否保留 `omv_camera.c` 方案，或收敛为 OpenMV 标准 `omv_csi` port 实现
- [ ] 评估是否需要进一步收敛到 OpenMV 标准 `omv_csi` `sensor` 框架

### Python 接口

- [x] `sensor.reset()` 可用
- [x] `sensor.snapshot()` 已接入
- [x] `sensor.skip_frames()` 已接入
- [x] `sensor` 基础方向控制接口已接入
- [x] `img.draw_*()` 已接入
- [x] `img.find_edges()` 已接入
- [ ] 完整 `image` 模块能力后续评估
- [ ] image 算法接口后续补充

### 内存与性能

- [x] 现阶段 `320x240` 预览路径可工作
- [ ] 评估 framebuffer 大小需求
- [ ] 确认 PSRAM 可用性和配置
- [ ] 评估 DMA 带宽限制（最大分辨率、帧率）
- [ ] 评估更高分辨率下的稳定性与帧率

### 验证

- [ ] sensor probe 成功
- [x] `sensor` 模块可导入
- [x] `sensor.snapshot()` 可正常返回图像
- [x] 抓到稳定图像（当前测试未见明显撕裂、无数据错误）
- [x] OpenMV IDE 预览帧可用
- [x] `img.draw_*()` 已验证能显示到 IDE 预览
- [x] 当前最小预览链路可工作
- [x] 结合 `docs/esp32p4/test_script.md` 完成板上脚本验证

## 验收标准

当前阶段验收：
- OpenMV IDE 能看到连续预览
- 真实 CSI 采图链路可稳定运行
- PPA 裁剪缩放 + JPEG 编码 + stream 推送整体链路稳定
- 最小 `sensor` 模块可用于基础抓图验证
- 最小 `draw_*` / `find_edges()` 能工作并刷新到 IDE
- 当前 `sensor` 测试脚本执行无明显问题

下一阶段验收：
- 支持至少一种已知可用的传感器
- 视需要继续补齐 `image` 算法和更完整的图像接口

## 参考

- ESP32-P4 CSI 控制器：ESP-IDF `esp_driver_cam` 组件
- OpenMV CSI 抽象层：`common/omv_csi.h`
- 当前实现文件：`ports/esp32/omv_camera.c`
- 当前实现文件：`ports/esp32/omv_test_preview.c`
- 当前实现文件：`ports/esp32/omv_framebuffer.c`
- 当前实现文件：`ports/esp32/py_sensor.c`
- 当前实现文件：`modules/py_image.c`
- 测试脚本：`docs/esp32p4/test_script.md`
