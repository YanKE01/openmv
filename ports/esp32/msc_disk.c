/*
 * TinyUSB MSC backing store for ESP32 internal flash filesystem partition.
 */
#include "tusb.h"

#if CFG_TUD_MSC

#include <string.h>

#include "py/mphal.h"
#include "esp_partition.h"

#define MSC_BLOCK_SIZE          (512)
#define MSC_ERASE_BLOCK_SIZE    (4096)

static const esp_partition_t *msc_part;
static bool msc_ejected;
static uint8_t msc_sector_cache[MSC_ERASE_BLOCK_SIZE];

static const esp_partition_t *msc_get_partition(void) {
    if (msc_part == NULL) {
        msc_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "ffat");
        if (msc_part == NULL) {
            msc_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "vfs");
        }
    }
    return msc_part;
}

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]) {
    memcpy(vendor_id, MICROPY_HW_USB_MSC_INQUIRY_VENDOR_STRING, 8);
    memcpy(product_id, MICROPY_HW_USB_MSC_INQUIRY_PRODUCT_STRING, 16);
    memcpy(product_rev, MICROPY_HW_USB_MSC_INQUIRY_REVISION_STRING, 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    if (msc_ejected || msc_get_partition() == NULL) {
        tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0x00);
        return false;
    }
    return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {
    const esp_partition_t *part = msc_get_partition();
    *block_size = MSC_BLOCK_SIZE;
    *block_count = part ? (part->size / MSC_BLOCK_SIZE) : 0;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
    if (load_eject) {
        msc_ejected = !start;
    }
    return true;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
    const esp_partition_t *part = msc_get_partition();
    if (part == NULL) {
        return -1;
    }

    uint32_t addr = lba * MSC_BLOCK_SIZE + offset;
    if (addr + bufsize > part->size) {
        return -1;
    }

    if (esp_partition_read(part, addr, buffer, bufsize) != ESP_OK) {
        return -1;
    }

    return bufsize;
}

static bool msc_write_chunk(const esp_partition_t *part, uint32_t addr, const uint8_t *buffer, uint32_t len) {
    while (len > 0) {
        uint32_t erase_base = (addr / MSC_ERASE_BLOCK_SIZE) * MSC_ERASE_BLOCK_SIZE;
        uint32_t erase_off = addr - erase_base;
        uint32_t write_len = MIN(len, MSC_ERASE_BLOCK_SIZE - erase_off);

        if (erase_off == 0 && write_len == MSC_ERASE_BLOCK_SIZE) {
            if (esp_partition_erase_range(part, erase_base, MSC_ERASE_BLOCK_SIZE) != ESP_OK) {
                return false;
            }
            if (esp_partition_write(part, erase_base, buffer, MSC_ERASE_BLOCK_SIZE) != ESP_OK) {
                return false;
            }
        } else {
            if (esp_partition_read(part, erase_base, msc_sector_cache, MSC_ERASE_BLOCK_SIZE) != ESP_OK) {
                return false;
            }
            memcpy(msc_sector_cache + erase_off, buffer, write_len);
            if (esp_partition_erase_range(part, erase_base, MSC_ERASE_BLOCK_SIZE) != ESP_OK) {
                return false;
            }
            if (esp_partition_write(part, erase_base, msc_sector_cache, MSC_ERASE_BLOCK_SIZE) != ESP_OK) {
                return false;
            }
        }

        addr += write_len;
        buffer += write_len;
        len -= write_len;
    }

    return true;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
    const esp_partition_t *part = msc_get_partition();
    if (part == NULL) {
        return -1;
    }

    uint32_t addr = lba * MSC_BLOCK_SIZE + offset;
    if (addr + bufsize > part->size) {
        return -1;
    }

    return msc_write_chunk(part, addr, buffer, bufsize) ? (int32_t) bufsize : -1;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize) {
    switch (scsi_cmd[0]) {
        case SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL:
            return 0;

        default:
            tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
            return -1;
    }
}

#endif
