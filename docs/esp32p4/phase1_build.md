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
