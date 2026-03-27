# Phase 2 — IDE 链路

**状态：进行中**

## 目标

- OpenMV IDE 能识别 ESP32P4 并建立会话
- 能下载运行脚本，能看到 REPL 输出
- 能访问板上文件系统

## 任务列表

### 通信方式确认

- [ ] 确认 ESP32P4 首版与 OpenMV IDE 的通信方式
  - 候选：USB CDC、USB Serial/JTAG、UART
  - 建议优先 USB CDC（ESP32P4 原生支持 USB OTG）

### IDE 识别

- [ ] 确认 USB VID/PID 已正确注册（当前 `0x37C5:0x1304`）
- [ ] 确认 IDE 能通过 VID/PID 识别并列出端口
- [ ] 确认板子产品名（`OMV_BOARD_TYPE`）在 IDE 中正确显示

### 协议适配

- [ ] 确认 OpenMV IDE 使用的通信协议（`protocol/` 目录）
- [ ] 在 `ports/esp32` 侧实现协议处理入口
- [ ] 验证 REPL 可用（脚本执行、`print` 输出回显）
- [ ] 验证脚本下载（IDE 下发 `.py` 脚本并执行）
- [ ] 验证文件系统访问（`ls`、`get`、`put` 操作）

### 启动行为

- [ ] 确认上电后自动运行 `main.py` 的机制
- [ ] 确认 IDE 连接时能中断自动运行并进入 REPL

## 验收标准

- OpenMV IDE 能识别板子并建立会话
- 能下载运行脚本，能看到 REPL 输出
- 能通过 IDE 访问板上文件系统

## 参考

- OpenMV 协议文档：`docs/protocol.md`
- ESP32P4 USB OTG 文档：ESP-IDF `usb/` 组件
