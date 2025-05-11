; syscall_asm.s - Assembly handler for system calls

[BITS 32]
section .text
global syscall_asm_handler
extern syscall_handle

; This handler is called when int 0x80 is executed
; Registers:
; eax = system call number
; ebx = arg1
; ecx = arg2
; edx = arg3
; esi = arg4
; edi = arg5
; ebp = arg6 (custom convention)
syscall_asm_handler:
    ; Save registers
    push ebp
    push edi
    push esi
    push edx
    push ecx
    push ebx
    
    ; Create syscall_args structure on stack
    sub esp, 24         ; 6 args * 4 bytes = 24 bytes
    mov [esp+0], ebx    ; arg1
    mov [esp+4], ecx    ; arg2
    mov [esp+8], edx    ; arg3
    mov [esp+12], esi   ; arg4
    mov [esp+16], edi   ; arg5
    mov [esp+20], ebp   ; arg6
    
    ; Call the C handler with syscall number and args pointer
    push esp            ; args pointer
    push eax            ; syscall number
    call syscall_handle
    
    ; Clean up stack
    add esp, 8          ; Remove parameters from stack
    add esp, 24         ; Remove syscall_args structure
    
    ; Restore registers
    pop ebx
    pop ecx
    pop edx
    pop esi
    pop edi
    pop ebp
    
    ; Return value is already in eax
    iret                ; Return from interrupt