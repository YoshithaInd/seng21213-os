#include "ramdisk.h"

/* The RAM disk itself: a fixed-size byte array living in BSS. (L12 §1) */
static uint8_t disk[RD_TOTAL_BLOCKS * RD_BLOCK_SIZE];

void ramdisk_init(void) {
    for (uint32_t i = 0; i < sizeof(disk); i++) disk[i] = 0;
}

void ramdisk_read(uint32_t block, void *buf) {
    if (block >= RD_TOTAL_BLOCKS) return;
    uint8_t *src = &disk[block * RD_BLOCK_SIZE];
    uint8_t *dst = (uint8_t *)buf;
    for (uint32_t i = 0; i < RD_BLOCK_SIZE; i++) dst[i] = src[i];
}

void ramdisk_write(uint32_t block, const void *buf) {
    if (block >= RD_TOTAL_BLOCKS) return;
    uint8_t *dst = &disk[block * RD_BLOCK_SIZE];
    const uint8_t *src = (const uint8_t *)buf;
    for (uint32_t i = 0; i < RD_BLOCK_SIZE; i++) dst[i] = src[i];
}