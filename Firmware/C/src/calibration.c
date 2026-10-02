#include "calibration.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

#define CAL_FLASH_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define CAL_MAGIC   0x53435043u  // "SCPC"
#define CAL_VERSION 2u  // v2: separate 48 MHz and 192 MHz ADC calibrations

typedef struct {
    uint32_t magic;
    uint32_t version;
    calibration_set_t cal;
    uint32_t checksum;
} cal_record_t;

_Static_assert(sizeof(cal_record_t) <= FLASH_PAGE_SIZE, "record must fit one flash page");

static uint32_t checksum(const cal_record_t *r) {
    // FNV-1a over everything before the checksum field.
    const uint8_t *p = (const uint8_t *)r;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < offsetof(cal_record_t, checksum); i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

bool calibration_load(calibration_set_t *cal) {
    const cal_record_t *r = (const cal_record_t *)(XIP_BASE + CAL_FLASH_OFFSET);
    if (r->magic != CAL_MAGIC || r->version != CAL_VERSION || r->checksum != checksum(r))
        return false;
    const float *v = (const float *)&r->cal;
    for (int i = 0; i < 16; i++)
        if (!isfinite(v[i])) return false;
    *cal = r->cal;
    return true;
}

void calibration_save(const calibration_set_t *cal) {
    static uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof page);
    cal_record_t r = {.magic = CAL_MAGIC, .version = CAL_VERSION, .cal = *cal};
    r.checksum = checksum(&r);
    memcpy(page, &r, sizeof r);

    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(CAL_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(CAL_FLASH_OFFSET, page, FLASH_PAGE_SIZE);
    restore_interrupts(irq);
}
