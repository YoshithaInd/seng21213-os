; SENG21213-OS :: Timer ISR (IRQ0 / vector 32) — L09 §5
[BITS 32]
[EXTERN irq0_handler]
[GLOBAL isr32]

isr32:
    pusha
    push ds
    push es
    push fs
    push gs

    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax

    push esp                 ; pass current stack pointer to C
    call irq0_handler        ; returns the stack pointer to RESUME (eax)
    add  esp, 4

    mov  esp, eax             ; switch stacks — may be a different process now

    pop  gs
    pop  fs
    pop  es
    pop  ds
    popa
    iret