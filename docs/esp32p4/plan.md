# ESP32P4 OpenMV Support Plan

## 目标

在当前 OpenMV 仓库内增加 `ports/esp32`，完成 ESP32P4 的完整接入。分五个阶段推进：

| 阶段 | 目标 | 状态 |
|------|------|------|
| Phase 1 | 打通编译，生成可烧录固件 | 完成 |
| Phase 2 | IDE 链路：脚本下载、REPL、预览 | 完成 |
| Phase 3 | CSI 摄像头真实采图 | 进行中（真实采图、标准 `py_image` 最小模式、IDE 预览已跑通） |
| Phase 4 | 外设和平台能力补全 | 待开始 |
| Phase 5 | 稳定化与提交收尾 | 待开始 |

详细任务见各阶段文档：

- [Phase 1 — 打通编译](phase1_build.md)
- [Phase 2 — IDE 链路](phase2_ide.md)
- [Phase 3 — CSI 摄像头](phase3_csi.md)
- [Phase 4 — 外设补全](phase4_peripherals.md)
- [Phase 5 — 稳定化收尾](phase5_stabilization.md)

## 已知前提

- ESP-IDF 环境路径：`/home/yanke/esp/esp-idf55/esp-idf`（版本 5.5.3）
- MicroPython 子模块已支持 `ESP32-P4`，上游板型目录：`lib/micropython/ports/esp32/boards/ESP32_GENERIC_P4`
- OpenMV 侧适配层落在 `ports/esp32/` 和 `boards/ESP32_GENERIC_P4/`
- 编译产物：`build/bin/firmware.bin`、`build/bin/micropython.bin`
- 当前已打通 IDE 预览，并接入最小 `sensor` Python 抓图路径
- 当前 `snapshot()` 已切到标准 `modules/py_image.c`，ESP32 侧以最小模式暴露 `draw_*`、`find_edges()` 和基础 `Image` 能力
- 旧 `py_image_lite` / `omv_imlib_*_min` 路径已下线

## 编译与烧录

```bash
# 第一步：导入 ESP-IDF 环境（每次新开终端都需要执行）
source /path/to/esp-idf/export.sh

# 编译
make TARGET=ESP32_GENERIC_P4 -j4

# 烧录
make TARGET=ESP32_GENERIC_P4 deploy ESPPORT=/dev/ttyACM0

# 串口监视
make TARGET=ESP32_GENERIC_P4 monitor ESPPORT=/dev/ttyACM0

# 擦除 Flash
make TARGET=ESP32_GENERIC_P4 erase ESPPORT=/dev/ttyACM0

# 清理
make TARGET=ESP32_GENERIC_P4 clean
```

## 已知注意事项

- `lib/micropython/ports/esp32/lockfiles/dependencies.lock.esp32p4` 中记录的是 ESP-IDF 5.5.1，本地使用 5.5.3，当前不阻塞编译，但整理提交时需要决定是否更新 lockfile。
- ESP32 不使用 OpenMV SDK（ARM 工具链），构建路径完全走 ESP-IDF + CMake。
