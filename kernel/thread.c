#include "thread.h"

/* All new threads' manufactured stack frames point EIP here, not at the
 * user's function directly — this lets us pass an argument, which
 * process_create()'s plain void(*)(void) signature can't. (L10 §1) */
static void thread_trampoline(void) {
    pcb_t *self         = process_current();
    void (*fn)(void *)  = self->thread_fn;
    void  *arg          = self->thread_arg;
    fn(arg);
    process_exit();
}

pcb_t *thread_create(void (*fn)(void *), void *arg) {
    pcb_t *p = process_create(thread_trampoline);
    if (!p) return 0;
    p->thread_fn  = fn;
    p->thread_arg = arg;
    return p;
}