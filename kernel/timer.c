#include "timer.h"

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* i8253 PIT oscillator runs at 1,193,180 Hz. (L09 §5) */
void timer_init(uint32_t freq_hz) {
    uint32_t divisor = 1193180 / freq_hz;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

extern uint32_t scheduler_tick(uint32_t esp);

uint32_t irq0_handler(uint32_t esp) {
    outb(0x20, 0x20);          /* EOI to master PIC */
    return scheduler_tick(esp);
}