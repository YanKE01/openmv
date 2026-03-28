# Phase 1 — 打通编译

**状态：完成**

## 目标

- OpenMV 顶层构建能识别 `esp32` port
- 新增 `ports/esp32` 适配层和 `boards/ESP32_GENERIC_P4` 板级目录
- `make TARGET=ESP32_GENERIC_P4` 能成功生成可烧录固件

## 不在本阶段范围内

- IDE 联机
- CSI 摄像头
- 真实外设驱动（GPIO/I2C/SPI/UART）

## 已完成的工作

### Makefile 改动

- 将 `include omv_boardconfig.mk` 提前到 SDK 检查之前，使 `PORT` 变量能在 SDK 检查时使用
- SDK 检查加 `ifneq ($(PORT),esp32)` 保护，ESP32 绕过 OpenMV SDK 依赖
- `size` 目标同样加保护，避免对 ESP32 ELF 调用 `arm-none-eabi-size`

### 新增文件

| 文件 | 说明 |
|------|------|
| `boards/ESP32_GENERIC_P4/omv_boardconfig.mk` | 设置 `PORT=esp32`、USB VID/PID |
| `boards/ESP32_GENERIC_P4/omv_boardconfig.h` | 板子 C 宏定义（架构名、UID 大小等） |
| `boards/ESP32_GENERIC_P4/manifest.py` | frozen 模块声明，冻结 `_boot.py` |
| `boards/ESP32_GENERIC_P4/imlib_config.h` | 图像库功能开关（最小集：仅 IMAGE_IO） |
| `boards/ESP32_GENERIC_P4/ulab_config.h` | ulab 配置（关闭复数，MAX_DIMS=4） |
| `micropython.cmake` | 顶层 CMake 入口，按 port 路由 |
| `ports/esp32/micropython.cmake` | 把 OpenMV 源码注入 ESP-IDF CMake 构建 |
| `ports/esp32/omv_mpconfigport.h` | 修改 REPL 启动横幅为 OpenMV 版本 |
| `ports/esp32/omv_portconfig.h` | 平台抽象层类型定义（int 占位） |
| `ports/esp32/omv_portconfig.mk` | ESP32 端口 Makefile（编译/烧录/监视入口） |

## 完整编译流程

### 概览

```
make TARGET=ESP32_GENERIC_P4
  └── Makefile (OpenMV 顶层)
        └── ports/esp32/omv_portconfig.mk
              └── idf.py build  (ESP-IDF + CMake)
                    └── lib/micropython/ports/esp32/CMakeLists.txt
                          └── main/CMakeLists.txt
                                └── esp32_common.cmake
                                      └── py/usermod.cmake  ← USER_C_MODULES
                                            └── micropython.cmake (OpenMV 顶层)
                                                  └── ports/esp32/micropython.cmake
                                                        └── 编译 modules/py_clock.c 等
```

---

### 第一层：OpenMV 顶层 `Makefile`

用户执行：

```bash
make TARGET=ESP32_GENERIC_P4 -j4
```

Makefile 按顺序做以下事情：

**① 确认 TARGET，加载板级配置**（`Makefile:22-32`）

```makefile
# Makefile:22-28 — TARGET 未指定时报错
ifeq ($(TARGET),)
  ifeq ($(filter sdk clean,$(MAKECMDGOALS)),)
    $(error Invalid or no TARGET specified)
  endif
endif

# Makefile:31-32 — 根据 TARGET 设置板级目录并立即 include
OMV_BOARD_CONFIG_DIR := $(CURDIR)/boards/$(TARGET)/
include $(OMV_BOARD_CONFIG_DIR)/omv_boardconfig.mk
```

`boards/ESP32_GENERIC_P4/omv_boardconfig.mk` 只有三行：

```makefile
# boards/ESP32_GENERIC_P4/omv_boardconfig.mk:1-3
PORT=esp32          # 告诉后续所有逻辑"这是 ESP32 构建"
OMV_USB_VID=0x37C5
OMV_USB_PID=0x1304
```

`PORT` 变量从这里来，后面所有条件判断都依赖它。**这就是为什么必须把 include 提前**——原来这行在第 144 行，那时 SDK 检查已经运行，`PORT` 还不存在。

**② 跳过 OpenMV SDK 检查**（`Makefile:40-51`）

```makefile
# Makefile:40-51
ifeq ($(filter sdk clean,$(MAKECMDGOALS)),)
  ifneq ($(PORT),esp32)   # ← ESP32 直接跳过整块检查
    ifeq ($(wildcard $(SDK_STAMP)),)
      $(error OpenMV SDK not found. Run 'make sdk' to install it.)
    ...
  endif
endif
```

ARM 构建需要 OpenMV SDK（里面有 gcc、llvm、cmake 等工具链）。ESP32 走 ESP-IDF 自己的工具链，不需要这个 SDK。

**③ 设置关键目录变量**（`Makefile:80-96`）

```makefile
# Makefile:80-96
export TOP_DIR   := $(shell pwd)          # 仓库根目录绝对路径
export BUILD     := $(TOP_DIR)/build      # 构建产物根目录
export MICROPY_DIR = lib/micropython      # MicroPython 子模块路径
export FW_DIR    := $(BUILD)/bin          # 最终固件输出目录
export OMV_LIB_DIR  := $(TOP_DIR)/scripts/libraries
export FROZEN_MANIFEST := $(OMV_BOARD_CONFIG_DIR)/manifest.py
                          # → boards/ESP32_GENERIC_P4/manifest.py
```

以及稍后（`Makefile:156-157`）：

```makefile
export OMV_PORT_DIR := $(TOP_DIR)/ports/$(PORT)
# → /home/yanke/project/openmv/ports/esp32
```

**④ 向 MicroPython 传递配置头文件路径**（`Makefile:270`）

```makefile
# Makefile:270
MPY_CFLAGS += -DMP_CONFIGFILE=\<$(OMV_PORT_DIR)/omv_mpconfigport.h\>
```

这让整个 MicroPython 编译时使用 `ports/esp32/omv_mpconfigport.h` 代替默认的 `mpconfigport.h`，从而在 REPL 启动时显示 OpenMV 版本信息。

**⑤ include 端口 Makefile，接管后续构建**（`Makefile:273`）

```makefile
# Makefile:272-273
# Include the port Makefile.
include $(OMV_PORT_DIR)/omv_portconfig.mk
# → include ports/esp32/omv_portconfig.mk
```

从这一行开始，构建目标（`all`、`$(FIRMWARE)` 等）全部由端口 Makefile 定义。

---

### 第二层：`ports/esp32/omv_portconfig.mk`

这是 ESP32 的构建入口，负责把控制权交给 ESP-IDF。

**前置检查**（`omv_portconfig.mk:7-9`）

```makefile
# ports/esp32/omv_portconfig.mk:7-9
ifeq ($(IDF_PATH),)
  $(error IDF_PATH is not set. Please source the ESP-IDF environment first: source $$IDF_PATH/export.sh)
endif
```

构建前必须先在 shell 中 source ESP-IDF 环境：

```bash
source /path/to/esp-idf/export.sh
```

source 之后，`IDF_PATH` 被设置、`idf.py` 进入 `PATH`，后续 Makefile 和 CMake 直接使用，不再需要在构建命令内部再 source 一次。

**关键变量**（`omv_portconfig.mk:11-12`）

```makefile
# ports/esp32/omv_portconfig.mk:11-12
ESP32_PORT_DIR := $(MICROPY_DIR)/ports/$(PORT)  # → lib/micropython/ports/esp32
ESP32_BUILD    := $(BUILD)/esp32                 # → build/esp32
```

**传递给 idf.py 的三个核心参数**（`omv_portconfig.mk:13-15`）

```makefile
# ports/esp32/omv_portconfig.mk:13-15
ESP32_IDF_ARGS = -D MICROPY_BOARD=$(TARGET) \           # → ESP32_GENERIC_P4
                 -DUSER_C_MODULES=$(TOP_DIR) \          # → /home/yanke/project/openmv
                 -D MICROPY_FROZEN_MANIFEST=$(FROZEN_MANIFEST)  # → boards/.../manifest.py
```

**实际执行的编译命令**（`omv_portconfig.mk:28-32`）

```makefile
# ports/esp32/omv_portconfig.mk:28-32
$(FIRMWARE):
    env -u BUILD bash -c "
      cd lib/micropython/ports/esp32 &&
      idf.py $(ESP32_IDF_ARGS) -B build/esp32 build &&
      python3 makeimg.py build/esp32/sdkconfig \
              build/esp32/bootloader/bootloader.bin \
              build/esp32/partition_table/partition-table.bin \
              build/esp32/micropython.bin \
              build/esp32/firmware.bin \
              build/esp32/micropython.uf2"
    mkdir -p build/bin
    cp build/esp32/firmware.bin build/bin/firmware.bin
    cp build/esp32/micropython.bin build/bin/micropython.bin
```

`env -u BUILD`：OpenMV 顶层 Makefile 在 `Makefile:81` 把 `BUILD` export 进了环境，但 `idf.py` 内部也用 `BUILD` 控制 CMake 构建目录，两者冲突会导致路径错乱，所以在这里清除它。

编译完成后，`makeimg.py` 把三段二进制（bootloader + 分区表 + micropython.bin）打包成单文件 `firmware.bin`，复制到 `build/bin/`。

---

### 第三层：ESP-IDF CMake 构建

`idf.py build` 进入 `lib/micropython/ports/esp32/`，读取 `CMakeLists.txt`：

**① 板型初始化**（`CMakeLists.txt:9-37`）

```cmake
# lib/micropython/ports/esp32/CMakeLists.txt:9-15
if(NOT MICROPY_BOARD)
    set(MICROPY_BOARD ESP32_GENERIC)
endif()
if(NOT MICROPY_BOARD_DIR)
    set(MICROPY_BOARD_DIR ${CMAKE_CURRENT_LIST_DIR}/boards/${MICROPY_BOARD})
    # → lib/micropython/ports/esp32/boards/ESP32_GENERIC_P4
endif()

# CMakeLists.txt:37
include(${MICROPY_BOARD_DIR}/mpconfigboard.cmake)
```

这里的 `boards/ESP32_GENERIC_P4` 是 **MicroPython 上游的板型目录**（在 `lib/micropython/` 内），负责设置 `IDF_TARGET=esp32p4`、sdkconfig 等。OpenMV 的 `boards/ESP32_GENERIC_P4/` 是另一套，只承担 OpenMV 层面的配置，不在这里被引用。

**② frozen manifest 处理**（`CMakeLists.txt:32-50`）

```cmake
# lib/micropython/ports/esp32/CMakeLists.txt:32
set(MICROPY_USER_FROZEN_MANIFEST ${MICROPY_FROZEN_MANIFEST})  # 先保存命令行传入值

# CMakeLists.txt:46-49  — 命令行传入的优先级最高
if (MICROPY_USER_FROZEN_MANIFEST)
    set(MICROPY_FROZEN_MANIFEST ${MICROPY_USER_FROZEN_MANIFEST})
    # → boards/ESP32_GENERIC_P4/manifest.py（OpenMV 的）
endif()
```

OpenMV 的 `boards/ESP32_GENERIC_P4/manifest.py`：

```python
freeze("$(OMV_LIB_DIR)/", "_boot.py")
```

其中 `$(OMV_LIB_DIR)` 是 Makefile 语法，在 CMake 中不会自动展开。解决方式是在 `ports/esp32/micropython.cmake:2` 显式设置同名 CMake 变量：

```cmake
# ports/esp32/micropython.cmake:2
set(MICROPY_MANIFEST_OMV_LIB_DIR "${OMV_TOP_DIR}/scripts/libraries")
```

MicroPython 的 frozen 脚本在处理 manifest 时会从 CMake 变量空间读取 `MICROPY_MANIFEST_OMV_LIB_DIR`，展开后找到 `scripts/libraries/_boot.py` 冻结进固件。

**③ ESP-IDF 组件发现，进入 `main/CMakeLists.txt`**（`main/CMakeLists.txt:7-11`）

```cmake
# lib/micropython/ports/esp32/main/CMakeLists.txt:7-11
if(NOT MICROPY_PORT_DIR)
    get_filename_component(MICROPY_PORT_DIR ${MICROPY_DIR}/ports/esp32 ABSOLUTE)
endif()
include(${MICROPY_PORT_DIR}/esp32_common.cmake)
```

`MICROPY_PORT_DIR` 在这里被设为绝对路径，后续 OpenMV 顶层 `micropython.cmake` 用它来判断当前是哪个 port。

**④ `esp32_common.cmake` 引入 usermod 机制**（`esp32_common.cmake:49`）

```cmake
# lib/micropython/ports/esp32/esp32_common.cmake:49
include(${MICROPY_DIR}/py/usermod.cmake)
```

---

### 第四层：`py/usermod.cmake` — USER_C_MODULES 展开（`usermod.cmake:43-57`）

```cmake
# lib/micropython/py/usermod.cmake:43-57
if (USER_C_MODULES)
    foreach(USER_C_MODULE_PATH ${USER_C_MODULES})
        # usermod.cmake:46-47 — 传入的是目录，自动追加 /micropython.cmake
        if (IS_DIRECTORY ${USER_C_MODULE_PATH})
            set(USER_C_MODULE_PATH "${USER_C_MODULE_PATH}/micropython.cmake")
        endif()
        # USER_C_MODULES = /home/yanke/project/openmv
        # → 找 /home/yanke/project/openmv/micropython.cmake
        include(${USER_C_MODULE_PATH})
    endforeach()
endif()
```

传入的是 OpenMV 根目录，`usermod.cmake:46-47` 自动追加 `/micropython.cmake`，找到 OpenMV 顶层的 `micropython.cmake`。

---

### 第五层：OpenMV 的两级 `micropython.cmake`

**顶层 `micropython.cmake` — 按 port 路由**（`micropython.cmake:1-6`）

```cmake
# micropython.cmake:1-6
if(DEFINED MICROPY_PORT_DIR)
    get_filename_component(OMV_PORT_NAME "${MICROPY_PORT_DIR}" NAME)
    # MICROPY_PORT_DIR = .../lib/micropython/ports/esp32，NAME 取最后一段 → "esp32"
    if(OMV_PORT_NAME STREQUAL "esp32")
        include(${CMAKE_CURRENT_LIST_DIR}/ports/esp32/micropython.cmake)
    endif()
endif()
```

这一层是未来支持多个 port 时的统一入口，各 port 有各自的 cmake 文件。

**`ports/esp32/micropython.cmake` — 注入 OpenMV 源码**（`ports/esp32/micropython.cmake:1-15`）

```cmake
# ports/esp32/micropython.cmake:1-2
get_filename_component(OMV_TOP_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(MICROPY_MANIFEST_OMV_LIB_DIR "${OMV_TOP_DIR}/scripts/libraries")
# ↑ 为 manifest.py 里的 $(OMV_LIB_DIR) 提供 CMake 侧的等价变量

# ports/esp32/micropython.cmake:4-15
add_library(usermod_openmv_esp32 INTERFACE)

target_sources(usermod_openmv_esp32 INTERFACE
    ${OMV_TOP_DIR}/modules/py_clock.c   # 当前唯一的 OpenMV C 模块
)

target_include_directories(usermod_openmv_esp32 INTERFACE
    ${OMV_TOP_DIR}                            # 顶层，找 omv_common.h 等
    ${OMV_TOP_DIR}/common                     # 找 omv_gpio.h 等抽象层头文件
    ${OMV_TOP_DIR}/modules                    # 找模块自己的头文件
    ${OMV_TOP_DIR}/ports/esp32                # 找 omv_portconfig.h、omv_mpconfigport.h
    ${OMV_TOP_DIR}/boards/${MICROPY_BOARD}    # 找 omv_boardconfig.h、imlib_config.h
)

# 挂载到 MicroPython 的 usermod 总线
target_link_libraries(usermod INTERFACE usermod_openmv_esp32)
```

`usermod` 是在 `py/usermod.cmake:2` 预定义的 INTERFACE 库，最终在 `esp32_common.cmake:288` 被链接进 `micropython` 目标。所有通过 `target_link_libraries(usermod INTERFACE ...)` 挂进来的模块都会成为固件的一部分。

**`target_compile_definitions` 是怎么影响 MicroPython / OpenMV 代码的**

```cmake
target_compile_definitions(usermod_openmv_esp32 INTERFACE
    MICROPY_OPENMV=1
    OMV_USB_STACK_TINYUSB=1
    OMV_USB_VID=0x37C5
    OMV_USB_PID=0x1204
    OMV_PROTOCOL_DEFAULT_CHANNELS=0
    OMV_PROTOCOL_HAS_FRAMEBUFFER=0
)
```

这里的关键点是：

- `usermod_openmv_esp32` 是一个 `INTERFACE` target，本身不单独产出 `.a` 或 `.o`
- 它携带的是一组“使用要求”，包括：
  - `target_sources`
  - `target_include_directories`
  - `target_compile_definitions`
  - `target_link_options`
- 这些使用要求会沿着依赖链继续向下传递

传播链如下：

```text
usermod_openmv_esp32
  └── usermod
        └── micropython
```

因此，这些 `target_compile_definitions(... INTERFACE ...)` 最终会变成 `micropython` 目标编译参数里的：

```text
-DMICROPY_OPENMV=1
-DOMV_USB_STACK_TINYUSB=1
-DOMV_USB_VID=0x37C5
-DOMV_USB_PID=0x1204
-DOMV_PROTOCOL_DEFAULT_CHANNELS=0
-DOMV_PROTOCOL_HAS_FRAMEBUFFER=0
```

这些宏最直接影响的，是在 `ports/esp32/micropython.cmake` 里通过 `target_sources(...)` 注入进来的 OpenMV 源码：

这里要特别注意一个容易误解的点：

- `boards/ESP32_GENERIC_P4/omv_boardconfig.mk` 里的 `OMV_USB_VID` / `OMV_USB_PID` 是 **Make 变量**
- `target_compile_definitions(...)` 里的 `OMV_USB_VID` / `OMV_USB_PID` 是 **传给 C 编译器的宏**

对于原生 OpenMV Make 构建，这两者通常在同一条 Make 链里，可以由 Makefile 继续向下转换和传递。  
但 ESP32 这条路径在进入 `idf.py + CMake` 之后，CMake 默认并不知道顶层 Makefile 里曾经定义过哪些普通变量。因此，如果不在 `ports/esp32/micropython.cmake` 里显式再定义一次，这些值就不会自动出现在 ESP32 固件源码的编译参数中。

也就是说，当前这里的重复定义，本质上是在做一件事：

```text
把 OpenMV 顶层 Make 世界里的板级配置
  └── 手动桥接到 ESP-IDF / CMake 世界里的编译宏
```

- `modules/py_clock.c`
- `protocol/omv_protocol.c`
- `protocol/omv_protocol_channel_stdio.c`
- `protocol/omv_protocol_channel_tinyusb.c`
- `common/omv_crc.c`

也就是说，这些文件虽然物理上位于 OpenMV 仓库顶层目录树下，但在 ESP32 构建里，它们已经作为 **MicroPython 最终固件目标的一部分** 被编译，因此会看到这些宏定义。

可以把它理解成：

```text
OpenMV 顶层源码
  └── 被挂进 micropython 目标
        └── 编译时自动带上这些 -D 宏
              └── 影响条件编译和常量值
```

需要注意的是：

- 这些宏不会影响整个 OpenMV 仓库所有代码
- 它们只影响 **本次 ESP32 构建中，属于 `micropython` 目标的那些编译单元**
- 如果某个 OpenMV 源文件没有被挂进这个 target，它就不会看到这些宏

因此，更准确地说：

- 这些 define 会影响“作为 ESP32 MicroPython 固件一部分被编译的 OpenMV 代码”
- 不会影响 `Makefile` 本身
- 不会影响 `ports/stm32` 那套 ARM 构建
- 也不会自动影响没有参与本次 target 的其他源码

---

### 编译产物

| 文件 | 位置 | 说明 |
|------|------|------|
| `micropython.bin` | `build/esp32/` | MicroPython 应用分区镜像 |
| `bootloader.bin` | `build/esp32/bootloader/` | ESP-IDF 二级 bootloader |
| `partition-table.bin` | `build/esp32/partition_table/` | 分区表 |
| `firmware.bin` | `build/esp32/` + `build/bin/` | 三段合并的完整烧录镜像 |
| `micropython.uf2` | `build/esp32/` | UF2 格式，拖拽烧录用 |

---

### 关键技术点汇总

| 问题 | 解法 |
|------|------|
| `PORT` 变量在 SDK 检查之前未知 | 将 `include omv_boardconfig.mk` 提前到 SDK 检查之前 |
| ESP32 不需要 OpenMV SDK | `ifneq ($(PORT),esp32)` 保护 SDK 检查块 |
| OpenMV 的 `BUILD` 变量污染 idf.py | `env -u BUILD` 清除再调用 idf.py |
| `manifest.py` 里的 `$(OMV_LIB_DIR)` 无法在 CMake 展开 | `ports/esp32/micropython.cmake` 中设置 `MICROPY_MANIFEST_OMV_LIB_DIR` |
| 如何把 OpenMV C 源码接入 ESP-IDF CMake | `USER_C_MODULES=$(TOP_DIR)` → `usermod.cmake` → OpenMV 的 `micropython.cmake` |
| 如何让 MicroPython 用 OpenMV 的配置头文件 | `MPY_CFLAGS += -DMP_CONFIGFILE=<omv_mpconfigport.h>` |

## 验收结果

```
build/bin/firmware.bin      ✓
build/bin/micropython.bin   ✓
make deploy                 ✓
make monitor                ✓
make erase                  ✓
make clean                  ✓
```

---

## 补充：完整的构建流程

这一节从更高一层的视角，把 **OpenMV 原本的构建流程** 和 **ESP32_GENERIC_P4 接入后的流程** 串起来，方便后续继续做 IDE、CSI、外设时定位改动应该落在哪一层。

### 1. OpenMV 原本的构建模型

OpenMV 的顶层构建不是“一个 Makefile 直接编所有板子”，而是一个 **两级分发模型**：

```text
make TARGET=<BOARD>
  └── Makefile
        ├── include boards/<BOARD>/omv_boardconfig.mk
        ├── 得到 PORT=<stm32/mimxrt/rp2/esp32/...>
        └── include ports/<PORT>/omv_portconfig.mk
```

也就是说：

- `TARGET` 决定当前要编哪块板子
- `boards/<BOARD>/omv_boardconfig.mk` 决定这块板子属于哪个 `PORT`
- `ports/<PORT>/omv_portconfig.mk` 决定真正的编译、链接、打包、烧录方式

因此，OpenMV 的顶层 `Makefile` 更像一个 **调度器**，而不是所有平台的最终构建实现。

---

### 2. 原生 OpenMV 板子的构建流程

以 `OPENMV4`、`OPENMV4P`、`OPENMV_N6` 这类 ARM 板子为例，流程大体如下：

```text
make TARGET=OPENMV4
  └── Makefile
        ├── 读取 boards/OPENMV4/omv_boardconfig.mk
        ├── 检查 OpenMV SDK
        ├── 计算 BUILD/FW_DIR/FROZEN_MANIFEST/MPY_MKARGS 等公共变量
        └── include ports/stm32/omv_portconfig.mk
              ├── 调用 MicroPython 子构建
              ├── 编译 OpenMV 自己的 C 源码
              ├── 链接 firmware.elf
              ├── 转成 firmware.bin
              ├── 如启用 bootloader，再编 bootloader 并拼接 openmv.bin
              └── 生成 romfs，并可 deploy
```

关键点：

- ARM 板子依赖 OpenMV SDK 提供的工具链和烧录工具
- OpenMV 顶层 Makefile 会把公共参数整理好，再交给 `ports/stm32/omv_portconfig.mk`
- `ports/stm32/omv_portconfig.mk` 会先触发一次 MicroPython 构建，然后再链接 OpenMV 固件

相关入口：

- `Makefile`
- `ports/stm32/omv_portconfig.mk`
- `common/micropy.mk`
- `boot/Makefile`

这条路径里，**OpenMV 自己是主导者**，MicroPython 是一个被调用的子构件。

---

### 3. ESP32_GENERIC_P4 接入后的构建流程

ESP32_GENERIC_P4 没有复用 ARM 那条路径，而是新增了一个 `esp32` port，把顶层构建分发到 ESP-IDF + MicroPython ESP32 port 上。

整体链路如下：

```text
make TARGET=ESP32_GENERIC_P4
  └── Makefile
        ├── include boards/ESP32_GENERIC_P4/omv_boardconfig.mk
        │     └── PORT=esp32
        ├── 跳过 OpenMV SDK 检查
        ├── 设置 BUILD/FW_DIR/FROZEN_MANIFEST/OMV_PORT_DIR 等公共变量
        └── include ports/esp32/omv_portconfig.mk
              └── idf.py build
                    └── lib/micropython/ports/esp32/CMakeLists.txt
                          └── MicroPython ESP32 board 配置
                          └── py/usermod.cmake
                                └── OpenMV 顶层 micropython.cmake
                                      └── ports/esp32/micropython.cmake
                                            ├── 注入 OpenMV C 源码
                                            └── 设置 manifest 相关变量
                    └── makeimg.py
                          ├── bootloader.bin
                          ├── partition-table.bin
                          ├── micropython.bin
                          └── firmware.bin
```

这一版接入后，OpenMV 顶层对 ESP32 的职责主要变成：

- 提供板级配置
- 提供 OpenMV 自己的模块、协议层、配置头文件和 frozen 脚本
- 把这些内容以 `USER_C_MODULES` 和 `MICROPY_FROZEN_MANIFEST` 的方式交给 MicroPython/ESP-IDF 构建系统

也就是说，这条路径里 **ESP-IDF + MicroPython 是主导者，OpenMV 是被注入进去的扩展层**。

---

### 4. `TARGET`、`PORT`、`BOARD` 三个概念的关系

在后续继续改代码时，最容易混淆的是这三个词：

| 名称 | 在哪里定义 | 作用 |
|------|------------|------|
| `TARGET` | 命令行传入，例如 `make TARGET=ESP32_GENERIC_P4` | 当前编译的板子名 |
| `PORT` | `boards/<TARGET>/omv_boardconfig.mk` | 当前板子属于哪条 port 构建链 |
| `MICROPY_BOARD` / `BOARD` | 传给 MicroPython 的板型名 | 给 MicroPython/ESP-IDF 选择对应 board 配置 |

对于本次接入：

```text
TARGET = ESP32_GENERIC_P4
PORT   = esp32
MICROPY_BOARD = ESP32_GENERIC_P4
```

虽然字符串相同，但含义不同：

- `TARGET` 是顶层 OpenMV 调度用的板子名
- `PORT` 是顶层路由到哪条构建链
- `MICROPY_BOARD` 是下游 MicroPython ESP32 port 使用的板型名

---

### 5. OpenMV board 目录和 MicroPython board 目录的区别

这次接入里存在两套 board 目录：

#### OpenMV 的 board 目录

```text
boards/ESP32_GENERIC_P4/
```

它负责：

- `PORT=esp32`
- OpenMV 自己的 `omv_boardconfig.h`
- `manifest.py`
- `imlib_config.h`
- `ulab_config.h`

#### MicroPython 的 board 目录

```text
lib/micropython/ports/esp32/boards/ESP32_GENERIC_P4/
```

它负责：

- ESP-IDF 目标芯片选择，例如 `esp32p4`
- `sdkconfig` 默认值
- MicroPython ESP32 port 自己的底层板级设置

两者同名，但服务的层级不同：

- OpenMV board 目录服务 OpenMV 顶层构建
- MicroPython board 目录服务 ESP32 port 底层构建

---

### 6. 冻结脚本和 C 源码接入是两条并行路径

ESP32 构建里，OpenMV 的内容是通过两条线接进去的：

#### 路径 A：C 源码

```text
USER_C_MODULES=$(TOP_DIR)
  └── py/usermod.cmake
        └── OpenMV 顶层 micropython.cmake
              └── ports/esp32/micropython.cmake
                    └── target_sources(... py_clock.c / omv_protocol.c / ...)
```

作用：

- 把 OpenMV 的 C 模块和协议层编进固件

#### 路径 B：frozen Python 脚本

```text
MICROPY_FROZEN_MANIFEST=boards/ESP32_GENERIC_P4/manifest.py
  └── MicroPython frozen manifest 处理
        └── freeze("$(PORT_DIR)/modules", "flashbdev.py")
        └── freeze("$(OMV_LIB_DIR)/", "_boot.py")
```

作用：

- 把 `flashbdev.py`、`_boot.py` 等启动脚本直接打进固件

这两条线都是 `idf.py build` 的一部分，但职责不同：

- 路径 A 负责“哪些 C 文件参与编译”
- 路径 B 负责“哪些 Python 文件被冻结进固件”

---

### 7. 最终产物是怎么形成的

ESP32 构建结束后，底层会先得到几段中间产物：

- `bootloader/bootloader.bin`
- `partition_table/partition-table.bin`
- `micropython.bin`

随后由 `makeimg.py` 打包成：

- `firmware.bin`
- `micropython.uf2`

最后复制到 OpenMV 统一输出目录：

- `build/bin/firmware.bin`
- `build/bin/micropython.bin`

与 ARM 路径不同的是：

- ARM 板子通常由 OpenMV 顶层 Make 亲自链接并在需要时拼接 `openmv.bin`
- ESP32 板子由 ESP-IDF 按 ESP32 固件布局生成镜像，OpenMV 只负责接入和转存产物

---

### 8. 一句话总结

OpenMV 原本的构建体系是：

```text
顶层 Makefile 负责调度
port Makefile 负责平台实现
```

而本次 ESP32_GENERIC_P4 接入，本质上是在这个体系里新增了一条新的 port 路线：

```text
OpenMV 顶层 Makefile
  └── 新增 PORT=esp32 分支
        └── 交给 ESP-IDF + MicroPython ESP32 port 构建
              └── 再把 OpenMV 的 C 模块和 frozen 脚本注入进去
```

因此，后续如果继续做：

- IDE 联机
- CSI 摄像头
- GPIO/I2C/SPI/UART
- 帧缓冲或图像链路

就可以按下面的思路判断该改哪里：

- 顶层分发问题：改 `Makefile` / `boards/<TARGET>/`
- ESP32 构建接入问题：改 `ports/esp32/omv_portconfig.mk`
- OpenMV 模块接入问题：改 `ports/esp32/micropython.cmake`
- 运行时启动和协议问题：改 `ports/esp32/main.c`、`protocol/`
- ESP32 芯片/SDK/board 底层问题：改 `lib/micropython/ports/esp32/boards/...`
