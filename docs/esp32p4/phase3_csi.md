# Phase 3 — CSI 摄像头

**状态：待开始**

## 目标

- 在 `ports/esp32` 实现真实的 CSI 摄像头驱动
- 完成 sensor probe、稳定采图
- OpenMV IDE 能看到基础预览帧

## 前置条件

- Phase 2（IDE 链路）完成

## 任务列表

### 硬件确认

- [ ] 确认目标开发板型号和 CSI 接口引脚定义
- [ ] 确认使用的摄像头传感器型号（OV2640、OV5640 等）
- [ ] 确认摄像头时钟、复位、PWDN 的 GPIO 编号

### 驱动实现

- [ ] 在 `ports/esp32/` 新增 `omv_csi.c`，从占位升级为真实驱动
- [ ] 实现摄像头时钟初始化
- [ ] 实现 I2C 探测（sensor probe）
- [ ] 实现复位和电源控制
- [ ] 实现像素数据采集（DMA/GDMA）

### 内存与性能

- [ ] 评估 framebuffer 大小需求
- [ ] 确认 PSRAM 可用性和配置
- [ ] 评估 DMA 带宽限制（最大分辨率、帧率）

### 验证

- [ ] sensor probe 成功
- [ ] 抓到稳定图像（无撕裂、无数据错误）
- [ ] OpenMV IDE 预览帧可用

## 验收标准

- `sensor.snapshot()` 返回有效图像
- OpenMV IDE 能看到连续预览
- 支持至少一种已知可用的传感器

## 参考

- ESP32-P4 CSI 控制器：ESP-IDF `esp_driver_cam` 组件
- OpenMV CSI 抽象层：`common/omv_csi.h`
