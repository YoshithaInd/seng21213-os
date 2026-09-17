#ifndef RAMDISK_H
#define RAMDISK_H
#include "../include/types.h"

#define RD_BLOCK_SIZE   4096
#define RD_TOTAL_BLOCKS 256   /* 256 * 4096 = 1 MB (L12 §1) */

void ramdisk_init(void);
void ramdisk_read(uint32_t block, void *buf);
void ramdisk_write(uint32_t block, const void *buf);

#endif