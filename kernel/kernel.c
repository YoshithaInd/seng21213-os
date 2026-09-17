/* =============================================================================
 * SENG21213-OS :: Main Kernel  (Stage 0 – Foundations)
 * File   : kernel/kernel.c
 *
 * PURPOSE
 *   This is the heart of your operating system. Right now it:
 *     1. Initialises VGA text-mode display
 *     2. Initialises the keyboard driver
 *     3. Prints a splash screen
 *     4. Runs a minimal interactive shell ("ksh")
 *
 * ASSIGNMENT MILESTONES  (what YOU will add in later lectures)
 *   Lecture  9  – Process Management  →  process.h / process.c / scheduler.c
 *   Lecture 10  – Threads             →  thread.h  / thread.c
 *   Lecture 11  – Memory Management   →  pmm.h     / pmm.c / vmm.c
 *   Lecture 12  – File System         →  fs.h      / fs.c
 *
 * CODING CONVENTION
 *   - Prefix kernel-internal functions with k_ (e.g. k_strcmp)
 *   - All driver APIs live in their own .h/.c pair
 *   - NEVER call malloc – use the PMM you build in Lecture 11
 * ============================================================================*/
#include "process.h"
#include "vga.h"
#include "keyboard.h"
#include "../include/types.h"
#include "idt.h"
#include "timer.h"
#include "thread.h"
#include "mutex.h"
#include "semaphore.h"


/* ---------------------------------------------------------------------------
 * Forward declarations of shell commands
 * --------------------------------------------------------------------------*/
static void cmd_help(void);
static void cmd_clear(void);
static void cmd_about(void);
static void cmd_echo(const char *args);
static void cmd_mem(void);
static void cmd_race(void);
static void cmd_pc(void);

/* ---------------------------------------------------------------------------
 * Utility: minimal string helpers (no libc in a freestanding kernel!)
 * --------------------------------------------------------------------------*/
static int k_strcmp(const char *a, const char *b) {
    while (*a && (*a == *b)) { a++; b++; }
    return (uint8_t)*a - (uint8_t)*b;
}

static int k_strncmp(const char *a, const char *b, size_t n) {
    while (n-- && *a && (*a == *b)) { a++; b++; }
    return n == (size_t)-1 ? 0 : (uint8_t)*a - (uint8_t)*b;
}

static size_t k_strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

/* Skip leading spaces */
static const char *k_ltrim(const char *s) {
    while (*s == ' ') s++;
    return s;
}

/* ---------------------------------------------------------------------------
 * Splash Screen
 * --------------------------------------------------------------------------*/
static void print_splash(void) {
    vga_clear(VGA_BLACK);

    /* Top banner box */
    vga_draw_box(0, 0, 7, 80, VGA_LIGHT_MAGENTA);

    vga_set_cursor(1, 2);
    vga_puts_color("  SENG21213-OS  |  Computer Architecture & Operating Systems",
                   VGA_YELLOW, VGA_BLACK);

    vga_set_cursor(2, 2);
    vga_puts_color("  Stage 0: Kernel Foundations", VGA_LIGHT_CYAN, VGA_BLACK);

    vga_set_cursor(3, 2);
    vga_puts_color("  Faculty of Engineering – Department of Software Engineering",
                   VGA_LIGHT_GREY, VGA_BLACK);

    vga_set_cursor(4, 2);
    vga_puts_color("  Built by students, for students.  Type 'help' to begin.",
                   VGA_LIGHT_GREEN, VGA_BLACK);

    vga_set_cursor(5, 2);
    vga_puts_color("  CPU: i686 (32-bit Protected Mode)  |  Display: VGA 80x25",
                   VGA_DARK_GREY, VGA_BLACK);

    vga_set_cursor(8, 0);
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    vga_puts("  Welcome! This kernel was compiled from source and booted entirely\n");
    vga_puts("  from bare metal. There is no Linux or Windows underneath – only\n");
    vga_puts("  the code you and your team write.\n");
    vga_puts("\n");
    vga_puts("  Assignment milestones to implement:\n");
    vga_puts_color("    [L09] ", VGA_YELLOW, VGA_BLACK);
    vga_puts("Process Management  – PCB, ready queue, round-robin scheduler\n");
    vga_puts_color("    [L10] ", VGA_YELLOW, VGA_BLACK);
    vga_puts("Threads & Sync      – kernel threads, mutex, semaphore\n");
    vga_puts_color("    [L11] ", VGA_YELLOW, VGA_BLACK);
    vga_puts("Memory Management   – physical page allocator, virtual memory\n");
    vga_puts_color("    [L12] ", VGA_YELLOW, VGA_BLACK);
    vga_puts("File System         – RAM disk, FAT-like directory structure\n");
    vga_puts("\n");
}

/* ---------------------------------------------------------------------------
 * Shell command implementations
 * --------------------------------------------------------------------------*/
static void cmd_help(void) {
    vga_puts_color("\n  SENG21213-OS Shell Commands\n", VGA_YELLOW, VGA_BLACK);
    vga_puts("  ─────────────────────────────────────────────\n");
    vga_puts("  help    – Show this help message\n");
    vga_puts("  clear   – Clear the screen\n");
    vga_puts("  about   – About this OS and course\n");
    vga_puts("  echo    – Echo text to screen\n");
    vga_puts("  mem     – Memory map (stub)\n");
    vga_puts_color("\n  Milestones (to implement):\n", VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("  ps      – [L09] List processes\n");
    vga_puts("  kill    – [L09] Terminate a process\n");
    vga_puts("  threads – [L10] List kernel threads\n");
    vga_puts("  free    – [L11] Show free memory\n");
    vga_puts("  ls      – [L12] List files\n");
    vga_puts("  cat     – [L12] Print file contents\n\n");
}

static void cmd_clear(void) {
    vga_clear(VGA_BLACK);
}

static void cmd_about(void) {
    vga_puts_color("\n  About SENG21213-OS\n", VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("  ─────────────────────────────────────────────\n");
    vga_puts("  Architecture : x86 (i686), 32-bit Protected Mode\n");
    vga_puts("  Bootloader   : Custom MBR (NASM)\n");
    vga_puts("  Kernel       : Freestanding C (GCC, no libc)\n");
    vga_puts("  VM Target    : QEMU (qemu-system-i386)\n");
    vga_puts("  Course       : SENG 21213 – Sem 2\n");
    vga_puts("  Reference    : Stallings, OS: Internals & Design Principles\n\n");
}

static void cmd_echo(const char *args) {
    vga_puts("  ");
    vga_puts(args);
    vga_puts("\n");
}

static void cmd_mem(void) {
    /* Stage 0 stub – students implement the real PMM in Lecture 11 */
    vga_puts_color("\n  Memory Map (stub – implement PMM in Lecture 11)\n",
                   VGA_LIGHT_CYAN, VGA_BLACK);
    vga_puts("  ─────────────────────────────────────────────\n");
    vga_puts("  0x00000000 – 0x000FFFFF  :  First 1 MB (reserved/BIOS)\n");
    vga_puts("  0x00100000 – 0x00EFFFFF  :  Extended memory (usable ~14 MB)\n");
    vga_puts("  0x00F00000 – 0x00FFFFFF  :  BIOS / ROM area\n");
    vga_puts("  0xB8000    – 0xBFFFF     :  VGA frame buffer\n");
    vga_puts_color("\n  TODO: Use BIOS int 0x15, EAX=0xE820 to get real memory map\n\n",
                   VGA_YELLOW, VGA_BLACK);
}


static const char *state_name(proc_state_t s) {
    switch (s) {
        case READY:      return "READY";
        case RUNNING:    return "RUNNING";
        case BLOCKED:    return "BLOCKED";
        case TERMINATED: return "TERMINATED";
    }
    return "?";
}


static void cmd_ps(void) {
    vga_puts_color("\n  PID  STATE\n", VGA_YELLOW, VGA_BLACK);
    vga_puts("  -----------------\n");
    for (int i = 0; i < MAX_PROCESSES; i++) {
        pcb_t *p = process_get(i);
        if (!p || p->state == TERMINATED) continue;
        vga_puts("  ");
        vga_printf("%d", p->pid);
        vga_puts("    ");
        vga_puts(state_name(p->state));
        vga_puts("\n");
    }
    vga_puts("\n");
}

/* ---------------------------------------------------------------------------
 * Stage 2 Demo: race condition, with and without a mutex (L10 §2)
 * --------------------------------------------------------------------------*/
#define RACE_ITERATIONS 5

static volatile int myglobal          = 0;
static volatile int race_threads_done = 0;
static mutex_t       race_mutex;
static int           race_use_mutex   = 0;

static void race_worker(void *arg) {
    (void)arg;
    for (int i = 0; i < RACE_ITERATIONS; i++) {
        if (race_use_mutex) mutex_lock(&race_mutex);

        int tmp = myglobal;
        for (volatile int d = 0; d < 500000; d++) { }  /* widen the race window */
        myglobal = tmp + 1;

        if (race_use_mutex) mutex_unlock(&race_mutex);
    }
    race_threads_done++;
}

static void cmd_race(void) {
    myglobal = 0; race_threads_done = 0; race_use_mutex = 0;
    mutex_init(&race_mutex);

    vga_puts_color("\n  Race condition demo (NO mutex)\n", VGA_LIGHT_RED, VGA_BLACK);
    vga_puts("  Two threads increment a shared counter without locking...\n");
    thread_create(race_worker, 0);
    thread_create(race_worker, 0);
    while (race_threads_done < 2) { __asm__ __volatile__("hlt"); }

    vga_puts("  Expected: "); vga_printf("%d", RACE_ITERATIONS * 2);
    vga_puts("   Actual: ");  vga_printf("%d", myglobal);
    if (myglobal != RACE_ITERATIONS * 2)
        vga_puts_color("  <-- CORRUPTED (race condition!)\n", VGA_LIGHT_RED, VGA_BLACK);
    else
        vga_puts_color("  (no corruption this run - timing dependent, try again)\n", VGA_YELLOW, VGA_BLACK);

    myglobal = 0; race_threads_done = 0; race_use_mutex = 1;
    vga_puts_color("\n  Race condition demo (WITH mutex)\n", VGA_LIGHT_GREEN, VGA_BLACK);
    thread_create(race_worker, 0);
    thread_create(race_worker, 0);
    while (race_threads_done < 2) { __asm__ __volatile__("hlt"); }

    vga_puts("  Expected: "); vga_printf("%d", RACE_ITERATIONS * 2);
    vga_puts("   Actual: ");  vga_printf("%d", myglobal);
    vga_puts_color("  <-- CORRECT every time\n\n", VGA_LIGHT_GREEN, VGA_BLACK);
}

/* ---------------------------------------------------------------------------
 * Stage 2 Demo: bounded-buffer producer-consumer, 3 semaphores (L10 §5)
 * --------------------------------------------------------------------------*/
#define PC_BUF_SIZE 5
#define PC_ITEMS    10

static int          pc_buffer[PC_BUF_SIZE];
static int          pc_in = 0, pc_out = 0;
static semaphore_t  pc_empty, pc_full, pc_mutex_sem;
static volatile int pc_produced_done = 0, pc_consumed_done = 0;

static void producer_thread(void *arg) {
    (void)arg;
    for (int i = 0; i < PC_ITEMS; i++) {
        sem_wait(&pc_empty);
        sem_wait(&pc_mutex_sem);
        pc_buffer[pc_in] = i;
        pc_in = (pc_in + 1) % PC_BUF_SIZE;
        sem_signal(&pc_mutex_sem);
        sem_signal(&pc_full);

        vga_puts("  Produced: "); vga_printf("%d\n", i);
        for (volatile int d = 0; d < 300000; d++) { }
    }
    pc_produced_done = 1;
}

static void consumer_thread(void *arg) {
    (void)arg;
    for (int i = 0; i < PC_ITEMS; i++) {
        sem_wait(&pc_full);
        sem_wait(&pc_mutex_sem);
        int item = pc_buffer[pc_out];
        pc_out = (pc_out + 1) % PC_BUF_SIZE;
        sem_signal(&pc_mutex_sem);
        sem_signal(&pc_empty);

        vga_puts("  Consumed: "); vga_printf("%d\n", item);
        for (volatile int d = 0; d < 300000; d++) { }
    }
    pc_consumed_done = 1;
}

static void cmd_pc(void) {
    pc_in = 0; pc_out = 0; pc_produced_done = 0; pc_consumed_done = 0;
    sem_init(&pc_empty, PC_BUF_SIZE);
    sem_init(&pc_full, 0);
    sem_init(&pc_mutex_sem, 1);

    vga_puts_color("\n  Producer-Consumer demo (3 semaphores)\n", VGA_LIGHT_CYAN, VGA_BLACK);
    thread_create(producer_thread, 0);
    thread_create(consumer_thread, 0);
    while (!(pc_produced_done && pc_consumed_done)) { __asm__ __volatile__("hlt"); }
    vga_puts_color("  Done. No data corruption, no deadlock.\n\n", VGA_LIGHT_GREEN, VGA_BLACK);
}


/*demo process*/
static void demo_process_a(void) {
    while (1) {
        vga_write_status(0, "[Process A running]", VGA_LIGHT_CYAN, VGA_BLACK);
        for (volatile int i = 0; i < 2000000; i++) { }
    }
}
static void demo_process_b(void) {
    while (1) {
        vga_write_status(40, "[Process B running]", VGA_LIGHT_GREEN, VGA_BLACK);
        for (volatile int i = 0; i < 2000000; i++) { }
    }
}

/* ---------------------------------------------------------------------------
 * Shell process
 * --------------------------------------------------------------------------*/
static char  shell_buf[256];
static char  prompt[] = "\n  ksh> ";




static void shell_run(void) {
    vga_puts_color("\n  Kernel Shell ready. Type 'help' for commands.\n",
                   VGA_LIGHT_GREEN, VGA_BLACK);

    while (true) {
        vga_puts_color(prompt, VGA_LIGHT_GREEN, VGA_BLACK);
        kb_readline(shell_buf, sizeof(shell_buf));

        /* Trim leading whitespace */
        const char *cmd = k_ltrim(shell_buf);
        if (k_strlen(cmd) == 0) continue;

        /* Dispatch */
        if (k_strcmp(cmd, "help")  == 0) { cmd_help();  continue; }
        if (k_strcmp(cmd, "clear") == 0) { cmd_clear(); continue; }
        if (k_strcmp(cmd, "about") == 0) { cmd_about(); continue; }
        if (k_strcmp(cmd, "mem")   == 0) { cmd_mem();   continue; }
        if (k_strcmp(cmd, "ps")    == 0) { cmd_ps();    continue; }
        if (k_strcmp(cmd, "race") == 0) { cmd_race(); continue; }
        if (k_strcmp(cmd, "pc")   == 0) { cmd_pc();   continue; }
        if (k_strncmp(cmd, "echo ", 5) == 0) {
            cmd_echo(k_ltrim(cmd + 5));
            continue;
        }

        /* Milestone stubs */
        if (k_strcmp(cmd, "kill")    == 0 ||
            k_strcmp(cmd, "threads") == 0 ||
            k_strcmp(cmd, "free")    == 0 ||
            k_strcmp(cmd, "ls")      == 0 ||
            k_strcmp(cmd, "cat")     == 0) {
            vga_puts_color("  [TODO] This command is not yet implemented.\n",
                           VGA_YELLOW, VGA_BLACK);
            vga_puts("  Implement it as part of your lecture assignment.\n");
            continue;
        }

        vga_puts_color("  Unknown command: ", VGA_LIGHT_RED, VGA_BLACK);
        vga_puts(cmd);
        vga_puts("\n  Type 'help' for a list of commands.\n");
    }
}

/* ---------------------------------------------------------------------------
 * Kernel entry point – called from kernel_entry.asm
 * --------------------------------------------------------------------------*/
void kernel_main(void) {
    vga_init();
    kb_init();
    process_init();
    idt_init();
    timer_init(100);

    process_create_current();
    process_create(demo_process_a);
    process_create(demo_process_b);

    __asm__ __volatile__("sti");

    print_splash();
    shell_run();

    /* Should never reach here */
    __asm__ __volatile__("hlt");
}
