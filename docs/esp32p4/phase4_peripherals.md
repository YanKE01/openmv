# Phase 4 — 外设和平台能力补全

**状态：待开始**

## 目标

补全 OpenMV 在 ESP32P4 上的外设抽象层，使 `machine` 模块和 OpenMV 外设 API 可用。

## 前置条件

- Phase 3（CSI 摄像头）完成

## 任务列表

### 基础外设

- [ ] `omv_gpio.c` — GPIO 读写（替换当前 `int` 占位类型）
- [ ] `omv_i2c.c` — I2C 主机驱动（用于传感器通信）
- [ ] `omv_spi.c` — SPI 驱动（用于显示屏、传感器扩展）
- [ ] `omv_uart.c` — UART 驱动

### GPIO 说明

OpenMV 这里有两层 GPIO 接口，需要区分：

- Python 层：`from machine import Pin`
- OpenMV C 层：`omv_gpio_*()`

本阶段要补的是 **OpenMV C 层 GPIO 抽象**，不是新增一个 Python 模块。  
也就是说，目标是让 OpenMV 自己的驱动、模块、传感器代码可以：

```c
#include "omv_gpio.h"
omv_gpio_config(...);
omv_gpio_write(...);
omv_gpio_read(...);
```

其中公共头文件在：

```text
common/omv_gpio.h
```

这个头定义了 OpenMV 统一的 GPIO 抽象接口，包括：

- `omv_gpio_config`
- `omv_gpio_deinit`
- `omv_gpio_read`
- `omv_gpio_write`
- `omv_gpio_irq_register`
- `omv_gpio_irq_enable`
- `omv_gpio_clock_enable`

需要注意的是，虽然公共头定义得比较完整，但不同 port 并不一定要在第一阶段一次性做满。  
例如 `rp2` 的实现就比较轻，只先覆盖了当前实际使用到的最小能力。

因此，ESP32P4 这边建议也采用同样的推进方式：

#### 第一阶段：最小 GPIO 支持

- `omv_gpio_config`
- `omv_gpio_deinit`
- `omv_gpio_read`
- `omv_gpio_write`

这四个接口足够先支撑基础外设 bring-up，以及后续逐步接入传感器、显示、控制引脚。

当前阶段的范围建议明确为：

- 先只承诺 basic GPIO
- 重点是 `read` / `write` 可用
- `irq_register` / `irq_enable` 暂不作为本阶段交付目标
- 不把 GPIO 中断回调链路提前算作“已支持”

#### 第二阶段：扩展能力

- `omv_gpio_irq_register`
- `omv_gpio_irq_enable`
- `omv_gpio_clock_enable`
- `omv_gpio_init0`
- 更完整的 `IT` / `OD` / `ALT` 模式语义

这样做的原因是：

- 先把最常用的 GPIO 能力补齐，降低 ESP32 port 的接入成本
- 避免在 Phase 4 一开始就被中断、DMA、复杂外设语义拖慢
- 与 `rp2` 这类轻量 port 的演进方式保持一致

### 连接能力

- [ ] 评估 Wi-Fi 是否纳入 OpenMV 功能面
- [ ] 评估 BLE 是否纳入

### 文件系统

- [ ] 确认 Flash 文件系统分区方案（LittleFS / FAT）
- [ ] 确认 SD 卡支持路径（SDMMC / SPI）
- [ ] 确认 `romfs`、frozen manifest 在 ESP32 上的行为

### 可选模块

- [ ] `display` — 评估 SPI/RGB 显示屏支持
- [ ] `audio` — 评估 I2S 麦克风/扬声器支持
- [ ] `ml` — 评估 ESP32P4 AI 加速能力集成

### `omv_portconfig.h` 类型替换

将以下占位类型替换为真实平台类型：

```c
// 当前占位
typedef int omv_gpio_t;
typedef int omv_i2c_dev_t;
typedef int omv_spi_dev_t;

// 替换为 ESP-IDF 实际类型
typedef gpio_num_t omv_gpio_t;
typedef i2c_master_dev_handle_t omv_i2c_dev_t;
typedef spi_device_handle_t omv_spi_dev_t;
```

## 验收标准

- `machine.Pin`、`machine.I2C`、`machine.SPI` 等 MicroPython 外设 API 正常工作
- OpenMV `sensor`、`image` 模块依赖的底层外设均有真实实现
- 文件系统可读写，`main.py` 自动运行机制正常

## 参考

- ESP-IDF GPIO 驱动：`driver/gpio.h`
- ESP-IDF I2C 驱动：`driver/i2c_master.h`
- ESP-IDF SPI 驱动：`driver/spi_master.h`
- OpenMV 外设抽象层：`common/omv_gpio.h`、`common/omv_i2c.h`
