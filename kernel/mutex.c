#include "mutex.h"

static inline void cli(void) { __asm__ __volatile__("cli"); }
static inline void sti(void) { __asm__ __volatile__("sti"); }

void mutex_init(mutex_t *m) {
    m->locked     = 0;
    m->wait_count = 0;
}

/* Blocking lock (L10 §2): a thread that can't acquire immediately is
 * marked BLOCKED and voluntarily yields — scheduler_tick() already
 * skips non-READY processes, so it simply never runs again until
 * mutex_unlock() sets it back to READY. */
void mutex_lock(mutex_t *m) {
    cli();
    if (!m->locked) {
        m->locked = 1;
        sti();
        return;
    }
    pcb_t *self = process_current();
    m->waiting[m->wait_count++] = self;
    self->state = BLOCKED;
    sti();
    process_yield();   /* returns once mutex_unlock() hands us the lock */
}

/* Hand-off unlock: if someone is waiting, the lock stays "held" and is
 * transferred directly to them, rather than being released and re-raced. */
void mutex_unlock(mutex_t *m) {
    cli();
    if (m->wait_count > 0) {
        pcb_t *next = m->waiting[0];
        for (int i = 1; i < m->wait_count; i++) m->waiting[i - 1] = m->waiting[i];
        m->wait_count--;
        next->state = READY;
    } else {
        m->locked = 0;
    }
    sti();
}