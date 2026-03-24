# ESP32P4 OpenMV Support Plan

## 目标

在当前 OpenMV 仓库内增加 `ports/esp32`，并先完成 `ESP32P4` 的最小可编译板级接入。第一阶段只追求：

- OpenMV 顶层构建能识别 `esp32` port。
- OpenMV 有自己的 `ports/esp32` 目录和板级目录。
- 能基于 `ESP32P4` 成功完成一次固件编译。

后续阶段再处理 IDE 连接、CSI 摄像头、传感器与稳定性收尾。

## 已知前提

- ESP-IDF 环境路径：`/home/yanke/esp/esp-idf55/esp-idf`
- 仓库内 vendored MicroPython 已支持 `ESP32-P4`：
  - `lib/micropython/ports/esp32`
  - `lib/micropython/ports/esp32/boards/ESP32_GENERIC_P4`
- 本次工作重点是把 OpenMV 自己的适配层落到 `openmv/ports/esp32`，而不是只依赖上游目录。
- 第一阶段不实现 `omv_gpio`、`omv_i2c`、`omv_csi`、`omv_spi` 等 OpenMV 外设层，只要求 OpenMV 顶层构建能成功产出镜像。

## 当前状态

更新时间：`2026-03-24 20:19:11 +0800`

### Phase 1 进展

- 已完成 `OpenMV -> ports/esp32` 的顶层构建接入。
- 已新增 OpenMV 侧最小 ESP32 端口骨架：
  - `ports/esp32/omv_portconfig.mk`
  - `ports/esp32/omv_portconfig.h`
  - `ports/esp32/omv_mpconfigport.h`
  - `ports/esp32/micropython.cmake`
- 已新增 OpenMV 侧板级目录：
  - `boards/ESP32_GENERIC_P4/omv_boardconfig.mk`
  - `boards/ESP32_GENERIC_P4/omv_boardconfig.h`
  - `boards/ESP32_GENERIC_P4/manifest.py`
  - `boards/ESP32_GENERIC_P4/imlib_config.h`
  - `boards/ESP32_GENERIC_P4/ulab_config.h`
- 已新增顶层 `micropython.cmake`，用于把 OpenMV 源码注入 `MicroPython esp32` 的 CMake 构建。
- 已确认第一阶段实际复用 upstream `lib/micropython/ports/esp32/boards/ESP32_GENERIC_P4` 作为 MicroPython 板型，OpenMV 自己的 `boards/ESP32_GENERIC_P4` 只承担 OpenMV 配置和 frozen manifest。
- 已解决首轮接入中的关键构建问题：
  - `BUILD` 环境变量污染 `mpy-cross`
  - `manifest.py` 中 `$(OMV_LIB_DIR)` 在 ESP32 CMake 构建中未传递
  - `time.clock()` 依赖的 `py_clock_type` 链接缺失
- 已完成编译验证：
  - `make TARGET=ESP32_GENERIC_P4 -j4`
  - 成功生成：
    - `build/esp32/micropython.bin`
    - `build/esp32/firmware.bin`
    - `build/bin/micropython.bin`
    - `build/bin/firmware.bin`
- 已补充基础开发辅助目标：
  - `make TARGET=ESP32_GENERIC_P4 clean`
  - `make TARGET=ESP32_GENERIC_P4 deploy ESPPORT=/dev/ttyACM0 [ESPBAUD=460800]`
  - `make TARGET=ESP32_GENERIC_P4 monitor ESPPORT=/dev/ttyACM0 [ESPBAUD=460800]`
  - `make TARGET=ESP32_GENERIC_P4 erase ESPPORT=/dev/ttyACM0`

### 第一阶段当前结论

- 第一阶段“只要求构建通过”的目标已达成。
- 当前还没有开始做 CSI 摄像头和 OpenMV 外设抽象层。
- 当前 `deploy/monitor` 只是复用 `idf.py flash/monitor`，还没有做 OpenMV IDE 侧联机集成。
- 已确认第二阶段优先级：先跑通 OpenMV IDE 连接，再做 CSI 摄像头。
- 已确认 `ESP32P4` 这边要按 `USB HS` 方向设计 IDE 通路，而不是把普通 UART/USB Serial-JTAG 当成最终方案。

### Phase 2 当前进展

- 已完成 `esp32` 端口的第二阶段最小 IDE 骨架接入，并确认 `make TARGET=ESP32_GENERIC_P4 -j4` 重新编译通过。
- 已在 `lib/micropython/ports/esp32/esp32_common.cmake` 增加主入口覆盖能力，允许 OpenMV 使用自有的 `ports/esp32/main.c` 接管启动主循环。
- 已新增 `ports/esp32/main.c`，当前主循环已切换为 OpenMV 风格：
  - 初始化 OpenMV protocol
  - 进入 `omv_protocol_exec_script()` + REPL 循环
  - 复用 `ESP32P4` upstream 的 `USB HS + TinyUSB CDC` 初始化链路
- 已在 `ports/esp32/micropython.cmake` 中接入最小协议源码集合：
  - `protocol/omv_protocol.c`
  - `protocol/omv_protocol_channel_tinyusb.c`
  - `protocol/omv_protocol_channel_stdio.c`
  - `common/omv_crc.c`
- 当前第二阶段只接入最小通道集合：
  - `omv_usb_channel`
  - `omv_stdin_channel`
  - `omv_stdout_channel`
- 已完成联板验证：
  - `USB HS` 已正常枚举为 CDC 设备
  - OpenMV IDE 已能识别板子并建立会话
  - 已抓到完整握手日志：`PROTO_SYNC` / `CHANNEL_LIST` / `PROTO_VERSION` / `SYS_INFO`
  - IDE 已可运行脚本
- 已确认运行态 `VID/PID` 采用 `0x37C5:0x1204` 可被 OpenMV IDE 正常识别。
- 已完成文件系统启动链路修复：
  - OpenMV `_boot.py` 已补上 `esp32 flashbdev` 识别
  - `boards/ESP32_GENERIC_P4/manifest.py` 已显式冻结 `flashbdev.py`
  - 首次全擦重刷后，`/flash` 文件系统可正常初始化，不再报 `readblocks` 异常
- 已将首次生成的默认 `main.py` 模板改为不依赖 `machine.LED` 的通用版本，避免 `ImportError: can't import name LED`。
- 当前明确不包含的内容：
  - `omv_stream_channel`
  - framebuffer 通道
  - CSI 摄像头采集链路
- 已完成若干 `esp32` 兼容性收口，避免 `CMSIS/NVIC/framebuffer` 等 ARM 假设阻塞 `ESP32P4` 编译。
- 当前仍保留了用于定位 IDE 握手的临时调试日志，后续需要在提交前移除。

### 已知后续事项

- `lib/micropython/ports/esp32/lockfiles/dependencies.lock.esp32p4` 会因为当前本地使用的是 `ESP-IDF 5.5.3` 而提示与 lockfile 中的 `5.5.1` 不一致。
- 这个问题当前不阻塞编译，但在整理提交时需要决定：
  - 是否更新 MicroPython 子模块中的 lockfile
  - 或保持现状并在提交说明中标明依赖版本差异

## 分阶段计划

### Phase 1: 新增 `ports/esp32` 和 `ESP32P4` 板级，先打通编译

#### 1. 构建路径打通

- 在 OpenMV 顶层构建中增加 `PORT=esp32` 分支。
- 新增 `ports/esp32/omv_portconfig.mk`，将实际编译委托给 `lib/micropython/ports/esp32` 的 `idf.py + CMake` 流程。
- 对 `esp32` 目标绕开当前默认的 `arm-none-eabi-*` 假设，避免复用 STM32/RP2 这套工具链变量。
- 将 ESP-IDF 初始化命令固定为：
  - `source /home/yanke/esp/esp-idf55/esp-idf/export.sh`

#### 2. OpenMV 侧端口目录

新增 `ports/esp32`，先放最小可编译适配层：

- `ports/esp32/omv_portconfig.mk`
- `ports/esp32/omv_portconfig.h`
- `ports/esp32/omv_mpconfigport.h`
- `ports/esp32/micropython.cmake`

说明：

- 第一阶段不引入 `omv_gpio.c`、`omv_i2c.c`、`omv_csi.c`、`omv_spi.c`，避免提前展开 OpenMV 外设抽象层。
- 若 OpenMV 根目录仍使用 `USER_C_MODULES=$(TOP_DIR)`，则增加顶层 `micropython.cmake`，内部再 include `ports/esp32/micropython.cmake`，把 OpenMV 源码正式接入 ESP32 的 CMake 构建。

#### 3. 板级目录

先新增一个与上游 MicroPython 板名对齐的 OpenMV 板级目录，建议首版直接使用：

- `boards/ESP32_GENERIC_P4`

最小文件集合：

- `boards/ESP32_GENERIC_P4/omv_boardconfig.mk`
- `boards/ESP32_GENERIC_P4/omv_boardconfig.h`
- `boards/ESP32_GENERIC_P4/manifest.py`
- `boards/ESP32_GENERIC_P4/imlib_config.h`
- `boards/ESP32_GENERIC_P4/ulab_config.h`

首版策略：

- 优先复用上游 `ESP32_GENERIC_P4`，减少第一阶段的板级分叉。
- 如果后续确认要做 OpenMV 专属硬件定义，再切出 `OPENMV_ESP32P4` 板型。
- 本阶段先关闭或保守配置未完成能力：`CSI`、`DISPLAY`、`AUDIO`、`FIR`、专用网络扩展等。

#### 4. 第一阶段的关键技术点

- 解决 OpenMV 顶层 Makefile 对 OpenMV SDK 和 ARM 工具链的默认依赖。
- 解决 OpenMV 源码注入 `MicroPython esp32` 的方式：
  - 优先方案：通过 `USER_C_MODULES + micropython.cmake` 接入。
  - 回退方案：必要时在 `ports/esp32` 增加更明确的 CMake 注入入口。
- 明确 `omv_portconfig.h` 里的 GPIO/I2C/SPI 类型映射，先满足 OpenMV 公共层编译。
- 明确 `omv_mpconfigport.h` 与上游 `mpconfigport.h` 的包含关系，避免重复定义。

#### 5. 编译验证

建议的首个验证命令：

```bash
make -C lib/micropython/mpy-cross
source /home/yanke/esp/esp-idf55/esp-idf/export.sh
make TARGET=ESP32_GENERIC_P4 clean
make TARGET=ESP32_GENERIC_P4
make TARGET=ESP32_GENERIC_P4 deploy ESPPORT=/dev/ttyACM0
make TARGET=ESP32_GENERIC_P4 monitor ESPPORT=/dev/ttyACM0
```

第一阶段验收标准：

- `make TARGET=ESP32_GENERIC_P4` 能跑到 `esp32` 构建路径。
- 生成完整固件产物，至少包括 `micropython.bin` / `firmware.bin` 一类镜像。
- OpenMV 自己的 `ports/esp32` 和 `boards/ESP32_GENERIC_P4` 已被实际使用。
- 已具备最小烧录和串口监视入口，方便后续联板调试。
- 不要求 IDE 可连接。
- 不要求 CSI 摄像头工作。

### Phase 2: OpenMV IDE 链路打通

- 以 `USB HS` 作为 `ESP32P4` 的首选连接路径，优先打通 OpenMV IDE，而不是先做 CSI。
- `esp32` 端口优先对齐 OpenMV 新协议路径：
  - 初始化 `omv_protocol_init_default()`
  - 挂接 `omv_usb_channel`
  - 复用 TinyUSB CDC transport，而不是旧的 `usbdbg` 专用实现
- 需要注意 OpenMV IDE 的 TinyUSB CDC transport 不是“普通串口可见”就够了：
  - `protocol/omv_protocol_channel_tinyusb.c` 通过 `CDC line coding`
  - 使用 `OMV_PROTOCOL_MAGIC_BAUDRATE=921600` 切换到 IDE protocol 模式
- `USB Serial/JTAG` 和 UART 可以保留为调试/REPL 回退，但不作为 OpenMV IDE 主通道。
- 验证项包括：
  - IDE 识别板子
  - 协议握手
  - 脚本下载执行
  - REPL/标准输出
  - 文件系统访问
- 处理 OpenMV IDE 依赖的板型识别、USB VID/PID、产品名、枚举方式和启动行为。
- 如有需要，在 `ports/esp32` 增加 TinyUSB/USB HS 相关 glue code，把 OpenMV protocol 和 MicroPython USB 回调正确接起来。

验收标准：

- OpenMV IDE 能识别板子并建立会话。
- 能下载运行脚本，能看到串口/REPL 输出。

当前下一步：

- 收掉 `esp32` 侧为 IDE 联机添加的临时调试日志。
- 继续补齐第二阶段剩余的体验项：
  - 校验文件系统读写与脚本保存
  - 校验 IDE 侧重连/软复位行为
- 第二阶段收尾后转入 CSI 摄像头接入。

### Phase 3: CSI 摄像头接入

- 明确使用的 ESP32P4 摄像头硬件路径和实际开发板引脚。
- 在 `ports/esp32/omv_csi.c` 中从占位实现升级为真实驱动。
- 打通摄像头时钟、复位、电源控制、I2C 探测、像素采集链路。
- 先支持单一已知可用的传感器组合，再扩展更多 sensor。
- 评估 framebuffer、大块内存、PSRAM 和 DMA/带宽约束。

验收标准：

- 能完成 sensor probe。
- 能抓到稳定图像。
- OpenMV IDE 能看到基础预览帧。

### Phase 4: 外设和平台能力补全

- SPI/I2C/UART/PWM 等基础外设能力补齐。
- 评估 Wi-Fi/BLE 是否纳入 OpenMV 功能面。
- 处理文件系统、启动脚本、romfs、frozen manifest 的差异。
- 视需要补 `display`、`audio`、`ml` 等模块的可用性。

### Phase 5: 稳定化和提交收尾

- 增加最小构建回归检查，保证 `esp32` 端口不会轻易编译回退。
- 整理板级默认配置和文档。
- 输出烧录步骤、已知限制、当前支持的摄像头型号。
- 评估是否需要新增 CI 构建任务。

## 当前建议的执行顺序

1. 建立 `ports/esp32` 最小目录和 OpenMV 板级目录。
2. 用 `make TARGET=ESP32_GENERIC_P4 clean && make TARGET=ESP32_GENERIC_P4` 打通编译路径。
3. 联板启动并确认基础 REPL/烧录链路可用。
4. 优先打通 `USB HS + TinyUSB CDC + OpenMV protocol` 的 IDE 链路。
5. IDE 通了以后，再做 `omv_csi.c` 的真实摄像头接入。

## 当前不纳入第一阶段的内容

- OpenMV IDE 联机可用性
- CSI 摄像头真实采图
- 复杂传感器驱动矩阵
- Wi-Fi/BLE 功能扩展
- 性能优化和量产配置
