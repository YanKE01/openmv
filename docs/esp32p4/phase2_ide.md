# Phase 2 — IDE 链路

**状态：完成**

## 目标

- OpenMV IDE 能识别 ESP32P4 并建立会话
- 能下载运行脚本，能看到 REPL 输出
- 能访问板上文件系统

## 已完成的工作

### 通信方式

确认以 **USB HS + TinyUSB CDC** 作为 ESP32P4 的 IDE 通信路径：

- ESP32P4 原生支持 USB OTG（HS），通过上游 MicroPython 的 TinyUSB CDC 初始化链路枚举为 CDC 设备
- `USB Serial/JTAG` 和 UART 保留为调试/REPL 回退，不作为 OpenMV IDE 主通道
- IDE 通过 `OMV_PROTOCOL_MAGIC_BAUDRATE=921600` line coding 切换进入 IDE protocol 模式

### 协议接入（`ports/esp32/main.c`、`ports/esp32/micropython.cmake`）

新增 `ports/esp32/main.c`，接管 MicroPython 的启动主循环：

- 调用 `omv_protocol_init_esp32()` 初始化协议，注册三个通道：`omv_usb_channel`、`omv_stdin_channel`、`omv_stdout_channel`
- 进入 `omv_protocol_exec_script()` + REPL 双循环结构
- `soft_reset` 时调用 `omv_protocol_deinit()` 清理

`micropython.cmake` 中通过 `MICROPY_SOURCE_PORT_MAIN` 机制注入此 `main.c`，替换 MicroPython 上游的 `main.c`（见 `esp32_common.cmake` 改动）。

参与编译的 OpenMV 协议源码：

| 文件 | 作用 |
|------|------|
| `protocol/omv_protocol.c` | 核心协议状态机 |
| `protocol/omv_protocol_channel_tinyusb.c` | USB CDC 通道实现 |
| `protocol/omv_protocol_channel_stdio.c` | stdin/stdout 通道实现 |
| `common/omv_crc.c` | CRC 校验 |

### USB 识别（VID/PID）

- 运行态 VID/PID：`0x37C5:0x1204`，可被 OpenMV IDE 正常识别
- 板名：`"OpenMV ESP32P4"`，制造商字符串：`"OpenMV"`
- 以上均通过 `MICROPY_OPENMV` 宏在 `mpconfigboard.h` 中覆盖上游默认值

### 协议兼容性修复（`protocol/omv_protocol.c`）

原始协议代码对 ARM 平台有多处硬依赖，已加条件编译支持 ESP32：

| 位置 | 原问题 | 修复方式 |
|------|--------|---------|
| `SYS_RESET` | 调用 `NVIC_SystemReset()` | 加 `__XTENSA__/__riscv` 分支调用 `esp_restart()` |
| `SYS_BOOT` | 调用 `MICROPY_BOARD_ENTER_BOOTLOADER` | ESP32 分支调用 `machine_bootloader()` |
| `SYS_INFO` | 读取 `SCB->CPUID`（ARM SCB 寄存器） | `__ARM_ARCH && SCB` 条件保护，ESP32 侧返回 0 |
| `SYS_INFO` | 读取 `OMV_BOARD_UID_ADDR`（未定义） | 加 `defined(OMV_BOARD_UID_ADDR)` 条件保护 |
| `SYS_INFO` | 引用 `framebuffer_get()` | `OMV_PROTOCOL_HAS_FRAMEBUFFER` 宏保护，ESP32 侧返回 0 |

`omv_protocol_channel_tinyusb.c` 移除了 `#include "cmsis_gcc.h"`；`omv_protocol_hw_caps.h` 改为用 `__has_include` 条件包含，避免 ESP32 编译时找不到该头文件。

### manifest.py 与 freeze 机制

**什么是 frozen module**

MicroPython 的 "frozen module" 是指在编译期间把 `.py` 文件直接嵌进固件二进制的机制。运行时这些模块从 Flash ROM 里直接执行，不占用 RAM，也不需要文件系统就能 `import`。

**freeze 的构建流程**

```
boards/ESP32_GENERIC_P4/manifest.py
  ↓ (由 idf.py -DMICROPY_FROZEN_MANIFEST=... 传入)
mkrules.cmake:313 — 调用 makemanifest.py
  ↓
mpy-cross 把 .py 编译为 .mpy 字节码
  ↓
生成 build/esp32/frozen_content.c
  ↓
作为普通 C 源文件链接进固件
```

**`manifest.py` 的内容解析**

```python
# boards/ESP32_GENERIC_P4/manifest.py

freeze("$(PORT_DIR)/modules", "flashbdev.py")
freeze("$(OMV_LIB_DIR)/", "_boot.py")
```

`freeze(directory, pattern)` 的含义：把 `directory` 里匹配 `pattern` 的文件冻结进固件。

`$(PORT_DIR)` 和 `$(OMV_LIB_DIR)` 是路径变量，在 `mkrules.cmake:293-308` 里展开：

| 变量 | 来源 | 展开后的路径 |
|------|------|-------------|
| `$(PORT_DIR)` | `MICROPY_MANIFEST_PORT_DIR` = `MICROPY_PORT_DIR` | `lib/micropython/ports/esp32` |
| `$(OMV_LIB_DIR)` | `MICROPY_MANIFEST_OMV_LIB_DIR`（由 `ports/esp32/micropython.cmake:2` 设置） | `scripts/libraries` |

所以两行的实际效果是：
- `freeze("lib/micropython/ports/esp32/modules", "flashbdev.py")` — 冻结 MicroPython 上游的 ESP32 Flash 块设备驱动
- `freeze("scripts/libraries/", "_boot.py")` — 冻结 OpenMV 的启动脚本

**上游 `manifest.py` 的对比**

上游 `lib/micropython/ports/esp32/boards/manifest.py` 是：

```python
freeze("$(PORT_DIR)/modules")   # 冻结 modules/ 下的全部文件
include("$(MPY_DIR)/extmod/asyncio")
require("bundle-networking")
...
```

它冻结了整个 `modules/` 目录（包括 `flashbdev.py`、`_boot.py`、`inisetup.py` 等），还引入了 asyncio 和网络库。

OpenMV 的 manifest 只冻结两个文件：
- 不引入网络库（OpenMV 当前不需要）
- 用 OpenMV 自己的 `_boot.py`（`scripts/libraries/_boot.py`）替换上游的（`modules/_boot.py`）
- 单独指定 `flashbdev.py` 而不是整个目录，保持最小化

**`OMV_LIB_DIR` 的特殊处理**

`$(OMV_LIB_DIR)` 不是 MicroPython 的内置变量，是 OpenMV 自己加的。`mkrules.cmake:301-308` 的逻辑是把所有 `MICROPY_MANIFEST_*` 前缀的 CMake 变量自动转为 `makemanifest.py` 的 `-v KEY=VALUE` 参数：

```cmake
# mkrules.cmake:296-298 — 内置变量
set(MICROPY_MANIFEST_PORT_DIR  ${MICROPY_PORT_DIR})   # → PORT_DIR=...
set(MICROPY_MANIFEST_BOARD_DIR ${MICROPY_BOARD_DIR})  # → BOARD_DIR=...
set(MICROPY_MANIFEST_MPY_DIR   ${MICROPY_DIR})        # → MPY_DIR=...

# ports/esp32/micropython.cmake:2 — OpenMV 新增变量
set(MICROPY_MANIFEST_OMV_LIB_DIR "${OMV_TOP_DIR}/scripts/libraries")
# → 自动转为 -v OMV_LIB_DIR=.../scripts/libraries 传给 makemanifest.py
```

### 文件系统启动链路

`_boot.py` 在固件启动时由 `main.c` 调用 `pyexec_frozen_module("_boot.py", false)` 执行，负责挂载文件系统和初始化 `/flash`。它依次尝试多种块设备：

```python
# scripts/libraries/_boot.py（简化）
bdev = None

# 1. 尝试 pyb.Flash（STM32）
# 2. 尝试 mimxrt.Flash（MIMXRT）
# 3. 尝试 rp2.Flash（RP2040）
# 4. 尝试 flashbdev（ESP32）← 本次新增
if bdev is None:
    from flashbdev import bdev as flash_bdev
    bdev = flash_bdev
```

ESP32 的 `flashbdev.py` 来自上游 `lib/micropython/ports/esp32/modules/flashbdev.py`，它通过分区表找到 `vfs` 或 `ffat` 分区作为文件系统块设备。不加这个分支时，`bdev` 为 `None`，后续 `vfs.VfsFat(bdev)` 会抛异常，导致 `/flash` 挂载失败。

`main.py` 模板也从依赖 `machine.LED` 的 blink 程序改为通用版本，避免在没有 LED 定义的板子上启动报错。

### MicroPython 子模块改动（`e9cf09b8`）

以下三处改动全部使用 `#ifdef MICROPY_OPENMV` 或 CMake 条件守护，**不影响原始 MicroPython 构建**，只在 OpenMV 构建时生效（`MICROPY_OPENMV=1` 由 `ports/esp32/micropython.cmake` 通过 `target_compile_definitions` 注入）。

---

**① `ports/esp32/boards/ESP32_GENERIC_P4/mpconfigboard.h` — 覆盖 USB 枚举信息**

```c
#ifdef MICROPY_OPENMV
#undef MICROPY_HW_BOARD_NAME
#define MICROPY_HW_BOARD_NAME               "OpenMV ESP32P4"
#define MICROPY_HW_USB_VID                  (0x37C5)
#define MICROPY_HW_USB_PID                  (0x1204)
#define MICROPY_HW_USB_PID_CDC              (MICROPY_HW_USB_PID)
#define MICROPY_HW_USB_PID_MSC              (MICROPY_HW_USB_PID)
#define MICROPY_HW_USB_PID_CDC_MSC          (MICROPY_HW_USB_PID)
#define MICROPY_HW_USB_MANUFACTURER_STRING  "OpenMV"
#define MICROPY_HW_USB_PRODUCT_FS_STRING    "OpenMV ESP32P4"
#endif
```

上游默认板名是 `"Generic ESP32P4 module"`，VID/PID 不是 OpenMV 的。这里先 `#undef` 再重定义，把 USB 枚举信息替换为 OpenMV IDE 能识别的值。`PID_CDC/MSC/CDC_MSC` 三个变体都指向同一个 PID，确保无论哪种 USB 描述符组合枚举出来，IDE 都能识别。

---

**② `ports/esp32/esp32_common.cmake` — 允许替换 `main.c`**

```cmake
# esp32_common.cmake:110-117
if(NOT DEFINED MICROPY_SOURCE_PORT_MAIN)
    set(MICROPY_SOURCE_PORT_MAIN main.c)
endif()

list(APPEND MICROPY_SOURCE_PORT
    ...
    ${MICROPY_SOURCE_PORT_MAIN}   # 原来硬编码 main.c
    ...
)
```

原来 `esp32_common.cmake` 中 `main.c` 是硬编码的字符串，无法替换。这里把它变量化：默认值仍是 `main.c`（不影响上游），但当外部（OpenMV 的 `micropython.cmake`）定义了 `MICROPY_SOURCE_PORT_MAIN` 时，就用 OpenMV 的 `ports/esp32/main.c` 替换，从而接管整个 MicroPython 的启动主循环。

---

**③ `ports/esp32/mpconfigport.h` — 注入三类运行时钩子**

```c
#ifdef MICROPY_OPENMV

// 钩子一：脚本执行前后通知 stdio 通道
#define MICROPY_BOARD_BEFORE_PYTHON_EXEC(input_kind, exec_flags) \
    do { stdio_channel_pyexec_hook(true); } while (0)
#define MICROPY_BOARD_AFTER_PYTHON_EXEC(input_kind, exec_flags, nlr, ret) \
    do { stdio_channel_pyexec_hook(false); } while (0)

// 钩子二：允许 VM 被外部中止
#define MICROPY_ENABLE_VM_ABORT             (1)

// 钩子三：重命名 TinyUSB CDC/事件回调，让 OpenMV 协议层先处理
#define MICROPY_WRAP_TUD_CDC_RX_CB(name)             __mp_ ## name
#define MICROPY_WRAP_TUD_CDC_LINE_STATE_CB(name)     __mp_ ## name
#define MICROPY_WRAP_TUD_EVENT_HOOK_CB(name)         __mp_ ## name

#endif
```

**`MICROPY_BOARD_BEFORE/AFTER_PYTHON_EXEC`**

MicroPython 的 `pyexec.c:69` 和 `:226` 在每次执行脚本前后调用这两个宏。OpenMV 用它通知 stdio 通道当前是否在跑用户脚本，`stdio_channel_pyexec_hook(true/false)` 让 stdin/stdout 通道在脚本执行期间正确路由输出，而不是混入协议帧。

**`MICROPY_ENABLE_VM_ABORT`**

开启后 MicroPython VM（`vm.c:1365`）会检查一个外部可设置的 abort 标志，允许协议层在收到 IDE 的中断命令时强制终止正在运行的脚本。不开启此选项时，只有 `Ctrl-C` 中断字符能打断 VM。

**`MICROPY_WRAP_TUD_CDC_RX_CB / LINE_STATE_CB / EVENT_HOOK_CB`**

这三个宏是 MicroPython TinyUSB 层的"弱符号重命名"机制（`mp_usbd_cdc.c:71`、`:175`、`mp_usbd.c:47`）。

不开启时：TinyUSB 的 CDC 数据到达回调（`tud_cdc_rx_cb`）直接由 MicroPython 处理，往 `stdin_ringbuf` 里塞数据。

开启后：MicroPython 原有实现被重命名为 `__mp_tud_cdc_rx_cb` 等，OpenMV 协议层得以定义同名的 `tud_cdc_rx_cb`，**优先处理 IDE protocol 帧**（magic baudrate 检测、帧解析等），只有在 REPL 模式时才转发给 MicroPython 原有实现。这是 IDE 通道和 REPL 能共用同一条 USB CDC 的关键机制。

## 验收结果

```
USB HS 枚举为 CDC 设备              ✓
OpenMV IDE 识别板子并建立会话        ✓
协议握手（SYNC / CHANNEL_LIST / VERSION / SYS_INFO）  ✓
脚本下载并执行                       ✓
REPL/标准输出可用                    ✓
/flash 文件系统初始化正常             ✓
```

## 已知遗留事项

- 开发过程中添加的临时调试日志需要在提交收尾前清理
- 文件系统读写与脚本保存的完整验证（`ls`/`get`/`put`）待确认
- IDE 侧重连/软复位行为待完整验证

## 参考

- 协议文档：`docs/protocol.md`
- ESP32P4 USB OTG：ESP-IDF `usb/` 组件、`esp_driver_usb_phy`
- MicroPython TinyUSB 通道：`lib/micropython/shared/tinyusb/`
