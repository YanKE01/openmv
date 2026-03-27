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
