#ifndef MUTEX_H
#define MUTEX_H
#include "process.h"

typedef struct {
    volatile int locked;
    pcb_t *waiting[MAX_PROCESSES];
    int    wait_count;
} mutex_t;

void mutex_init(mutex_t *m);
void mutex_lock(mutex_t *m);
void mutex_unlock(mutex_t *m);
#endif