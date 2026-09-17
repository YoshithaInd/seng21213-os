#include "semaphore.h"

static inline void cli(void) { __asm__ __volatile__("cli"); }
static inline void sti(void) { __asm__ __volatile__("sti"); }

void sem_init(semaphore_t *s, int value) {
    s->count      = value;
    s->wait_count = 0;
}

/* Classic Stallings counting-semaphore wait: decrement, and if it went
 * negative, block until sem_signal() wakes us. (L10 §3) */
void sem_wait(semaphore_t *s) {
    cli();
    s->count--;
    if (s->count < 0) {
        pcb_t *self = process_current();
        s->waiting[s->wait_count++] = self;
        self->state = BLOCKED;
        sti();
        process_yield();
        return;
    }
    sti();
}

void sem_signal(semaphore_t *s) {
    cli();
    s->count++;
    if (s->count <= 0 && s->wait_count > 0) {
        pcb_t *next = s->waiting[0];
        for (int i = 1; i < s->wait_count; i++) s->waiting[i - 1] = s->waiting[i];
        s->wait_count--;
        next->state = READY;
    }
    sti();
}