#ifndef PROCESS_H
#define PROCESS_H

#include "../include/types.h"

#define MAX_PROCESSES    16
#define STACK_SIZE     4096

typedef enum { READY, RUNNING, BLOCKED, TERMINATED } proc_state_t;

typedef struct pcb {
    uint32_t      pid;
    proc_state_t  state;
    uint32_t      esp;
    uint32_t      eip;
    uint32_t      stack[STACK_SIZE / 4];
    struct pcb   *next;
    void (*thread_fn)(void *);
void  *thread_arg;
} pcb_t;

void     process_init(void);
pcb_t   *process_create(void (*entry)(void));
pcb_t   *process_create_current(void);
pcb_t   *process_get(int index);
pcb_t *process_current(void);
void     process_yield(void);
void     process_exit(void);
uint32_t scheduler_tick(uint32_t esp);

#endif