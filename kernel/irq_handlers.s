; irq.s - Exception and IRQ handlers for uintOS
[BITS 32]

section .text

; External C functions
extern irq_common_handler
extern exception_common_handler

; Global ISRs for CPU exceptions (0-31)
global isr0
global isr1
global isr2
global isr3
global isr4
global isr5
global isr6
global isr7
global isr8
global isr9
global isr10
global isr11
global isr12
global isr13
global isr14
global isr15
global isr16
global isr17
global isr18
global isr19
global isr20
global isr21
global isr22
global isr23
global isr24
global isr25
global isr26
global isr27
global isr28
global isr29
global isr30
global isr31

; Global IRQ handlers (32-47)
global irq0
global irq1
global irq2
global irq3
global irq4
global irq5
global irq6
global irq7
global irq8
global irq9
global irq10
global irq11
global irq12
global irq13
global irq14
global irq15

; CPU Exceptions (0-31)
; These push the error code if the CPU doesn't
; otherwise we push a dummy error code to keep the stack frame consistent

; Divide by Zero Exception (0)
isr0:
    push dword 0     ; Push dummy error code
    push dword 0     ; Push interrupt number
    jmp exception_common_stub

; Debug Exception (1)
isr1:
    push dword 0     ; Push dummy error code
    push dword 1     ; Push interrupt number
    jmp exception_common_stub

; Non-maskable Interrupt Exception (2)
isr2:
    push dword 0     ; Push dummy error code
    push dword 2     ; Push interrupt number
    jmp exception_common_stub

; Breakpoint Exception (3)
isr3:
    push dword 0     ; Push dummy error code
    push dword 3     ; Push interrupt number
    jmp exception_common_stub

; Overflow Exception (4)
isr4:
    push dword 0     ; Push dummy error code
    push dword 4     ; Push interrupt number
    jmp exception_common_stub

; Bound Range Exceeded Exception (5)
isr5:
    push dword 0     ; Push dummy error code
    push dword 5     ; Push interrupt number
    jmp exception_common_stub

; Invalid Opcode Exception (6)
isr6:
    push dword 0     ; Push dummy error code
    push dword 6     ; Push interrupt number
    jmp exception_common_stub

; Device Not Available Exception (7)
isr7:
    push dword 0     ; Push dummy error code
    push dword 7     ; Push interrupt number
    jmp exception_common_stub

; Double Fault Exception (8) - Has error code
isr8:
    ; Error code is already pushed by CPU
    push dword 8     ; Push interrupt number
    jmp exception_common_stub

; Coprocessor Segment Overrun (9)
isr9:
    push dword 0     ; Push dummy error code
    push dword 9     ; Push interrupt number
    jmp exception_common_stub

; Invalid TSS Exception (10) - Has error code
isr10:
    ; Error code is already pushed by CPU
    push dword 10    ; Push interrupt number
    jmp exception_common_stub

; Segment Not Present (11) - Has error code
isr11:
    ; Error code is already pushed by CPU
    push dword 11    ; Push interrupt number
    jmp exception_common_stub

; Stack-Segment Fault (12) - Has error code
isr12:
    ; Error code is already pushed by CPU
    push dword 12    ; Push interrupt number
    jmp exception_common_stub

; General Protection Fault (13) - Has error code
isr13:
    ; Error code is already pushed by CPU
    push dword 13    ; Push interrupt number
    jmp exception_common_stub

; Page Fault (14) - Has error code
isr14:
    ; Error code is already pushed by CPU
    push dword 14    ; Push interrupt number
    jmp exception_common_stub

; Reserved (15)
isr15:
    push dword 0     ; Push dummy error code
    push dword 15    ; Push interrupt number
    jmp exception_common_stub

; x87 Floating-Point Exception (16)
isr16:
    push dword 0     ; Push dummy error code
    push dword 16    ; Push interrupt number
    jmp exception_common_stub

; Alignment Check Exception (17) - Has error code
isr17:
    ; Error code is already pushed by CPU
    push dword 17    ; Push interrupt number
    jmp exception_common_stub

; Machine Check Exception (18)
isr18:
    push dword 0     ; Push dummy error code
    push dword 18    ; Push interrupt number
    jmp exception_common_stub

; SIMD Floating-Point Exception (19)
isr19:
    push dword 0     ; Push dummy error code
    push dword 19    ; Push interrupt number
    jmp exception_common_stub

; Virtualization Exception (20)
isr20:
    push dword 0     ; Push dummy error code
    push dword 20    ; Push interrupt number
    jmp exception_common_stub

; Control Protection Exception (21)
isr21:
    ; Error code is already pushed by CPU
    push dword 21    ; Push interrupt number
    jmp exception_common_stub

; Reserved (22-31)
%assign i 22
%rep 10
isr%+i:
    push dword 0     ; Push dummy error code
    push dword i     ; Push interrupt number
    jmp exception_common_stub
%assign i i+1
%endrep

; IRQ handlers (32-47)
%assign i 0
%rep 16
irq%+i:
    push dword 0     ; Push dummy error code
    push dword (i+32) ; IRQs are mapped to ISRs 32-47
    jmp irq_common_stub
%assign i i+1
%endrep

; Common stub for CPU exceptions
exception_common_stub:
    ; Save registers
    pushad                  ; Pushes eax, ecx, edx, ebx, esp, ebp, esi, edi

    ; Call C exception handler with pointer to the stack frame
    mov eax, esp            ; Stack frame pointer as first argument
    push eax
    call exception_common_handler
    add esp, 4              ; Clean up stack

    ; Restore registers
    popad

    ; Clean up error code and interrupt number
    add esp, 8

    ; Return from interrupt
    iret

; Common stub for IRQ handlers
irq_common_stub:
    ; Save registers
    pushad                  ; Pushes eax, ecx, edx, ebx, esp, ebp, esi, edi

    ; Call C handler
    mov eax, [esp + 32]     ; Get the interrupt number
    mov ebx, [esp + 36]     ; Get the error code
    push ebx
    push eax
    call irq_common_handler
    add esp, 8              ; Clean up stack

    ; Restore registers
    popad

    ; Clean up error code and interrupt number
    add esp, 8

    ; Return from interrupt
    iret
