#include "pmm.h"

#define FRAME_SIZE      4096
#define E820_COUNT_ADDR 0x8000
#define E820_BUF_ADDR   0x8004
#define MAX_E820        32

typedef struct __attribute__((packed)) {
    uint64_t base;
    uint64_t length;
    uint32_t type;      /* 1 = usable RAM */
    uint32_t acpi_attr;
} e820_entry_t;

/* One frame's worth of bitmap covers 4096*8 = 32768 frames = 128 MB.
 * Plenty for a teaching OS running with -m 32M. (L11 §2) */
#define BITMAP_FRAMES   32768
static uint8_t  bitmap[BITMAP_FRAMES / 8];

static uint32_t total_frames = 0;
static uint32_t used_frames  = 0;

static inline void bitmap_set(uint32_t frame)   { bitmap[frame / 8] |=  (1 << (frame % 8)); }
static inline void bitmap_clear(uint32_t frame) { bitmap[frame / 8] &= ~(1 << (frame % 8)); }
static inline int  bitmap_test(uint32_t frame)  { return bitmap[frame / 8] & (1 << (frame % 8)); }

void pmm_init(void) {
    /* Start fully reserved; only usable E820 regions get cleared below.
     * (L11 §2 — safer default than assuming unmapped memory is free) */
    for (uint32_t i = 0; i < BITMAP_FRAMES / 8; i++) bitmap[i] = 0xFF;

    uint32_t     count = *(volatile uint32_t *)E820_COUNT_ADDR;
    e820_entry_t *entries = (e820_entry_t *)E820_BUF_ADDR;

    for (uint32_t i = 0; i < count && i < MAX_E820; i++) {
        if (entries[i].type != 1) continue;   /* only "usable RAM" regions */

        uint64_t start = entries[i].base;
        uint64_t end   = entries[i].base + entries[i].length;
        uint32_t f_start = (uint32_t)(start / FRAME_SIZE);
        uint32_t f_end   = (uint32_t)(end   / FRAME_SIZE);
        if (f_end > BITMAP_FRAMES) f_end = BITMAP_FRAMES;

        for (uint32_t f = f_start; f < f_end; f++) bitmap_clear(f);
        if (f_end > total_frames) total_frames = f_end;
    }

    /* First 1 MB (BIOS/IVT/our own kernel) is always reserved, regardless
     * of what E820 reports — never hand this range out. (L11 §2) */
    uint32_t reserved_low = (1024 * 1024) / FRAME_SIZE;
    for (uint32_t f = 0; f < reserved_low && f < BITMAP_FRAMES; f++) bitmap_set(f);

    used_frames = 0;
    for (uint32_t f = 0; f < total_frames; f++)
        if (bitmap_test(f)) used_frames++;
}

/* First-fit scan (L11 §3) */
uint32_t pmm_alloc_frame(void) {
    for (uint32_t f = 0; f < total_frames; f++) {
        if (!bitmap_test(f)) {
            bitmap_set(f);
            used_frames++;
            return f * FRAME_SIZE;
        }
    }
    return 0;   /* 0 = out of memory (frame 0 is always reserved, so safe sentinel) */
}

void pmm_free_frame(uint32_t paddr) {
    uint32_t f = paddr / FRAME_SIZE;
    if (f >= total_frames) return;
    if (bitmap_test(f)) {
        bitmap_clear(f);
        used_frames--;
    }
}

uint32_t pmm_total_frames(void) { return total_frames; }
uint32_t pmm_used_frames(void)  { return used_frames; }
uint32_t pmm_free_frames(void)  { return total_frames - used_frames; }