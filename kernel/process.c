#include "process.h"

static pcb_t   process_table[MAX_PROCESSES];
static pcb_t  *current_process = 0;
static pcb_t  *ready_head      = 0;
static uint32_t next_pid       = 0;

void process_init(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_table[i].state = TERMINATED;
        process_table[i].next  = 0;
    }
    current_process = 0;
    ready_head      = 0;
    next_pid        = 0;
}

static void enqueue(pcb_t *p) {
    if (!ready_head) { ready_head = p; p->next = p; return; }
    pcb_t *tail = ready_head;
    while (tail->next != ready_head) tail = tail->next;
    tail->next = p;
    p->next    = ready_head;
}

static pcb_t *find_free_slot(void) {
    for (int i = 0; i < MAX_PROCESSES; i++)
        if (process_table[i].state == TERMINATED) return &process_table[i];
    return 0;
}

pcb_t *process_create(void (*entry)(void)) {
    pcb_t *p = find_free_slot();
    if (!p) return 0;

    p->pid   = next_pid++;
    p->state = READY;
    p->eip   = (uint32_t)entry;

    /* Fake "interrupted" stack frame so the first resume behaves exactly
     * like resuming any other process. Layout must match isr32's
     * pop order exactly: gs,fs,es,ds / popa / iret. (L09 §6) */
    uint32_t *sp = (uint32_t *)&p->stack[STACK_SIZE / 4];

    *(--sp) = 0x202;             /* EFLAGS - IF set          */
    *(--sp) = 0x08;              /* CS - kernel code segment */
    *(--sp) = (uint32_t)entry;   /* EIP                      */
    *(--sp) = 0;  /* EAX */
    *(--sp) = 0;  /* ECX */
    *(--sp) = 0;  /* EDX */
    *(--sp) = 0;  /* EBX */
    *(--sp) = 0;  /* dummy ESP (discarded by popa) */
    *(--sp) = 0;  /* EBP */
    *(--sp) = 0;  /* ESI */
    *(--sp) = 0;  /* EDI */
    *(--sp) = 0x10; /* DS */
    *(--sp) = 0x10; /* ES */
    *(--sp) = 0x10; /* FS */
    *(--sp) = 0x10; /* GS */

    p->esp = (uint32_t)sp;
    enqueue(p);
    return p;
}

/* Registers whoever is CURRENTLY executing (the shell) as a real PCB, so
 * the first timer tick has somewhere to save its live register state. */
pcb_t *process_create_current(void) {
    pcb_t *p = find_free_slot();
    if (!p) return 0;
    p->pid   = next_pid++;
    p->state = RUNNING;
    p->esp   = 0;
    enqueue(p);
    current_process = p;
    return p;
}

pcb_t *process_current(void) {
    return current_process;
}

pcb_t *process_get(int index) {
    if (index < 0 || index >= MAX_PROCESSES) return 0;
    return &process_table[index];
}

void process_yield(void) {
    __asm__ __volatile__("int $32");
}

void process_exit(void) {
    if (current_process) current_process->state = TERMINATED;
    __asm__ __volatile__("int $32");
    while (1) { }
}

/* Called from kernel/isr.asm on every PIT tick. Round-robin over the
 * circular ready queue, skipping anything not READY. (L09 §4) */
uint32_t scheduler_tick(uint32_t esp) {
    if (current_process) {
        current_process->esp = esp;
        if (current_process->state == RUNNING) current_process->state = READY;
    }
    if (!ready_head) return esp;

    pcb_t *start = current_process ? current_process->next : ready_head;
    pcb_t *next  = start;
    do {
        if (next->state == READY) break;
        next = next->next;
    } while (next != start);

    if (next->state != READY) { current_process = 0; return esp; }

    next->state     = RUNNING;
    current_process = next;
    return next->esp;
}