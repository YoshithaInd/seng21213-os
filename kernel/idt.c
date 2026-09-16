#include "idt.h"
#include "../include/types.h"

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

typedef struct __attribute__((packed)) {
    uint16_t base_low;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_high;
} idt_entry_t;

typedef struct __attribute__((packed)) {
    uint16_t limit;
    uint32_t base;
} idt_ptr_t;

static idt_entry_t idt[256];
static idt_ptr_t   idtp;

extern void isr32(void);   /* defined in kernel/isr.asm */

static void idt_set_gate(int n, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[n].base_low  = (uint16_t)(base & 0xFFFF);
    idt[n].base_high = (uint16_t)((base >> 16) & 0xFFFF);
    idt[n].sel       = sel;
    idt[n].always0   = 0;
    idt[n].flags     = flags;
}

/* Remap the 8259 PIC so hardware IRQs land at vectors 32-47 instead of
 * colliding with CPU exception vectors 0-31. (L09 §5) */
static void pic_remap(void) {
    outb(0x20, 0x11); outb(0xA0, 0x11);
    outb(0x21, 0x20); outb(0xA1, 0x28);
    outb(0x21, 0x04); outb(0xA1, 0x02);
    outb(0x21, 0x01); outb(0xA1, 0x01);
    outb(0x21, 0x00); outb(0xA1, 0x00);
}

void idt_init(void) {
    pic_remap();
    /* Mask every hardware IRQ except IRQ0 (timer). The keyboard is still
     * read by polling (keyboard.c), so its interrupt (IRQ1) is unused -
     * masking it prevents an unhandled-vector fault when a key is pressed. */
    outb(0x21, 0xFE);   /* Master PIC: unmask only bit 0 (IRQ0) */
    outb(0xA1, 0xFF);   /* Slave PIC: mask everything */
    for (int i = 0; i < 256; i++) idt_set_gate(i, 0, 0, 0);

    /* Vector 32 = IRQ0 (PIT timer) after remap. 0x8E = present|ring0|32-bit interrupt gate */
    idt_set_gate(32, (uint32_t)isr32, 0x08, 0x8E);

    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (uint32_t)&idt;
    __asm__ __volatile__("lidt %0" : : "m"(idtp));
}