# SPDX-License-Identifier: MIT
#
# Copyright (C) 2026 OpenMV, LLC.
#
# ESP32 Makefile.

ifeq ($(IDF_PATH),)
  $(error IDF_PATH is not set. Please source the ESP-IDF environment first: source $$IDF_PATH/export.sh)
endif

ESP32_PORT_DIR := $(MICROPY_DIR)/ports/$(PORT)
ESP32_BUILD := $(BUILD)/esp32
ESP32_IDF_ARGS = -D MICROPY_BOARD=$(TARGET) \
                 -DUSER_C_MODULES=$(TOP_DIR) \
                 -D MICROPY_FROZEN_MANIFEST=$(FROZEN_MANIFEST)
ESP32_IDF_DEVICE_ARGS =

ifdef ESPPORT
ESP32_IDF_DEVICE_ARGS += -p $(ESPPORT)
endif

ifdef ESPBAUD
ESP32_IDF_DEVICE_ARGS += -b $(ESPBAUD)
endif

all: $(OPENMV)

$(FIRMWARE):
	$(Q)env -u BUILD -u FROZEN_MANIFEST bash -c "cd $(ESP32_PORT_DIR) && idf.py $(ESP32_IDF_ARGS) -B $(ESP32_BUILD) build && python3 makeimg.py $(ESP32_BUILD)/sdkconfig $(ESP32_BUILD)/bootloader/bootloader.bin $(ESP32_BUILD)/partition_table/partition-table.bin $(ESP32_BUILD)/micropython.bin $(ESP32_BUILD)/firmware.bin $(ESP32_BUILD)/micropython.uf2"
	$(Q)mkdir -p $(FW_DIR)
	$(Q)cp $(ESP32_BUILD)/firmware.bin $(FW_DIR)/firmware.bin
	$(Q)cp $(ESP32_BUILD)/micropython.bin $(FW_DIR)/micropython.bin

$(OPENMV): $(FIRMWARE)

size:
	$(Q)env -u BUILD -u FROZEN_MANIFEST bash -c "cd $(ESP32_PORT_DIR) && idf.py $(ESP32_IDF_ARGS) -B $(ESP32_BUILD) size"

deploy: $(FIRMWARE)
	$(Q)cd $(ESP32_PORT_DIR) && idf.py $(ESP32_IDF_ARGS) -B $(ESP32_BUILD) $(ESP32_IDF_DEVICE_ARGS) flash

menuconfig:
	$(Q)cd $(ESP32_PORT_DIR) && idf.py $(ESP32_IDF_ARGS) -B $(ESP32_BUILD) $(ESP32_IDF_DEVICE_ARGS) menuconfig

erase:
	$(Q)cd $(ESP32_PORT_DIR) && idf.py $(ESP32_IDF_ARGS) -B $(ESP32_BUILD) $(ESP32_IDF_DEVICE_ARGS) erase-flash

monitor:
	$(Q)cd $(ESP32_PORT_DIR) && idf.py $(ESP32_IDF_ARGS) -B $(ESP32_BUILD) $(ESP32_IDF_DEVICE_ARGS) monitor
