#ifndef SEMAPHORE_H
#define SEMAPHORE_H
#include "process.h"

typedef struct {
    volatile int count;
    pcb_t *waiting[MAX_PROCESSES];
    int    wait_count;
} semaphore_t;

void sem_init(semaphore_t *s, int value);
void sem_wait(semaphore_t *s);
void sem_signal(semaphore_t *s);
#endif