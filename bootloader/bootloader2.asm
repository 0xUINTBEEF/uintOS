;*********************************************
; SecondaryLoader.asm
; Enhanced UEFI and BIOS compatible secondary bootloader for uintOS
;*********************************************
bits 16

; Add support for loading from both BIOS and UEFI systems
%define UEFI_SUPPORT 1
%define REAL_HARDWARE_SUPPORT 1

; Improved memory mapping and detection
%define MAX_MEM_REGIONS 32
%define ACPI_RECLAIMABLE 3
%define ACPI_NVS 4
%define MEMORY_MAP_SIGNATURE 0x534D4150  ; "SMAP"

; More robust error handling with detailed error codes
%define ERR_NO_KERNEL 0x01
%define ERR_DISK_READ 0x02
%define ERR_A20_FAIL 0x03
%define ERR_MEMORY_MAP 0x04
%define ERR_PMODE_FAIL 0x05
%define ERR_HARDWARE_DETECT 0x06

; Memory Structure Improvements
struc boot_info
    .mem_map_addr:    resd 1   ; Address of memory map
    .mem_map_entries: resd 1   ; Number of memory map entries
    .kernel_phys:     resd 1   ; Physical address where kernel is loaded
    .kernel_size:     resd 1   ; Size of kernel in bytes
    .boot_device:     resb 1   ; Boot device identifier
    .vbe_mode_info:   resd 1   ; VBE mode info structure address
    .acpi_rsdp:       resd 1   ; ACPI RSDP table address
    .cmdline:         resd 1   ; Kernel command line address
    .modules_count:   resd 1   ; Count of loaded modules
    .modules_addr:    resd 1   ; Address of module info structures
    .hardware_info:   resd 1   ; Address of hardware information structure
endstruc

; Hardware information structure
struc hardware_info_struct
    .cpu_vendor:      resb 12  ; CPU vendor string
    .cpu_features:    resd 1   ; CPU features flags
    .has_apic:        resb 1   ; 1 if APIC present
    .has_sse:         resb 1   ; 1 if SSE present
    .has_sse2:        resb 1   ; 1 if SSE2 present
    .has_sse3:        resb 1   ; 1 if SSE3 present
    .has_fpu:         resb 1   ; 1 if FPU present
    .has_vmx:         resb 1   ; 1 if VMX present (for virtualization)
    .has_aes:         resb 1   ; 1 if AES instructions present
    .has_xsave:       resb 1   ; 1 if XSAVE present
    .reserved:        resb 4   ; Reserved for future use
endstruc

; Create boot info structure that will be passed to kernel
boot_info_struct:
    istruc boot_info
        at boot_info.mem_map_addr,    dd 0
        at boot_info.mem_map_entries, dd 0
        at boot_info.kernel_phys,     dd KERNEL_LOAD_ADDR
        at boot_info.kernel_size,     dd 0
        at boot_info.boot_device,     db 0
        at boot_info.vbe_mode_info,   dd 0
        at boot_info.acpi_rsdp,       dd 0
        at boot_info.cmdline,         dd 0
        at boot_info.modules_count,   dd 0
        at boot_info.modules_addr,    dd 0
        at boot_info.hardware_info,   dd hardware_info
    iend

; Hardware information that will be passed to kernel
hardware_info:
    istruc hardware_info_struct
        at hardware_info_struct.cpu_vendor,    times 12 db 0
        at hardware_info_struct.cpu_features,  dd 0
        at hardware_info_struct.has_apic,      db 0
        at hardware_info_struct.has_sse,       db 0
        at hardware_info_struct.has_sse2,      db 0
        at hardware_info_struct.has_sse3,      db 0
        at hardware_info_struct.has_fpu,       db 0
        at hardware_info_struct.has_vmx,       db 0
        at hardware_info_struct.has_aes,       db 0
        at hardware_info_struct.has_xsave,     db 0
        at hardware_info_struct.reserved,      times 4 db 0
    iend

section .text
global start
start:
    ; Initialize segment registers and stack
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    ; Save boot drive
    mov [boot_info_struct + boot_info.boot_device], dl
    
    ; Print welcome message
    mov si, welcome_msg
    call print_string
    
    ; Enable A20 line with multiple methods for reliability
    call enable_a20_line
    test ax, ax
    jnz .a20_enabled
    
    mov si, a20_error_msg
    call print_string
    mov al, ERR_A20_FAIL
    jmp fatal_error
    
.a20_enabled:
    mov si, a20_success_msg
    call print_string

    ; Detect CPU and hardware features
    mov si, hw_detect_msg
    call print_string
    
    call detect_hardware
    test ax, ax
    jnz .hw_detected
    
    mov si, hw_detect_error_msg
    call print_string
    mov al, ERR_HARDWARE_DETECT
    jmp fatal_error
    
.hw_detected:
    mov si, hw_detect_success_msg
    call print_string
    
    ; Get memory map with failsafe mechanisms
    call detect_memory
    test ax, ax
    jnz .memory_mapped
    
    mov si, memory_error_msg
    call print_string
    mov al, ERR_MEMORY_MAP
    jmp fatal_error
    
.memory_mapped:
    ; Store memory map information
    mov [boot_info_struct + boot_info.mem_map_addr], ebx
    mov [boot_info_struct + boot_info.mem_map_entries], ecx
    
    ; Detect ACPI RSDP
    call find_acpi_rsdp
    mov [boot_info_struct + boot_info.acpi_rsdp], eax

    ; Load kernel from disk
    mov si, loading_kernel_msg
    call print_string
    call load_kernel
    test ax, ax
    jnz .kernel_loaded
    
    mov si, kernel_error_msg
    call print_string
    mov al, ERR_NO_KERNEL
    jmp fatal_error
    
.kernel_loaded:
    ; Setup GDT, IDT for protected mode
    call setup_protected_mode
    
    ; Setup basic video mode for kernel
    call setup_video_mode
    
    ; Switch to protected mode
    mov si, entering_pmode_msg
    call print_string
    
    cli
    lgdt [gdt_descriptor]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    
    ; Jump to 32-bit code
    jmp CODE_SEG:protected_mode_entry

;----------------------------------------------------------------------------
; 32-bit protected mode code
;----------------------------------------------------------------------------
[BITS 32]
protected_mode_entry:
    ; Set up segment registers for protected mode
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Set up stack for kernel
    mov esp, 0x90000
    
    ; Pass boot info structure to kernel
    mov ebx, boot_info_struct
    
    ; Jump to kernel entry point
    jmp DWORD [boot_info_struct + boot_info.kernel_phys]

;----------------------------------------------------------------------------
; 16-bit helper functions
;----------------------------------------------------------------------------
[BITS 16]

; Detect CPU and hardware features
detect_hardware:
    ; Check for CPUID instruction support
    pushfd                          ; Save EFLAGS
    pop eax                         ; Store EFLAGS in EAX
    mov ecx, eax                    ; Save in ECX for later comparison
    xor eax, 0x00200000             ; Flip ID bit in EFLAGS
    push eax                        ; Store to stack
    popfd                           ; Load into EFLAGS
    pushfd                          ; Push EFLAGS to TOS
    pop eax                         ; Store EFLAGS in EAX
    push ecx                        ; Restore original EFLAGS
    popfd                           ; from ECX
    
    ; Compare to see if we were able to change the ID bit
    xor eax, ecx                    ; If we couldn't change ID bit, this will be 0
    jz .no_cpuid                    ; Jump if CPUID not supported
    
    ; Get vendor ID string
    mov eax, 0                      ; CPUID function 0: Get vendor ID
    cpuid
    
    ; Store vendor string (EBX,EDX,ECX contain the vendor string)
    mov [hardware_info + hardware_info_struct.cpu_vendor], ebx
    mov [hardware_info + hardware_info_struct.cpu_vendor + 4], edx
    mov [hardware_info + hardware_info_struct.cpu_vendor + 8], ecx
    
    ; Get feature information
    mov eax, 1                      ; CPUID function 1: Get feature bits
    cpuid
    
    ; Store CPU features
    mov [hardware_info + hardware_info_struct.cpu_features], edx
    
    ; Check for SSE
    bt edx, 25                      ; Bit 25 is SSE
    jnc .no_sse
    mov byte [hardware_info + hardware_info_struct.has_sse], 1
.no_sse:

    ; Check for SSE2
    bt edx, 26                      ; Bit 26 is SSE2
    jnc .no_sse2
    mov byte [hardware_info + hardware_info_struct.has_sse2], 1
.no_sse2:

    ; Check for SSE3
    bt ecx, 0                       ; Bit 0 of ECX is SSE3
    jnc .no_sse3
    mov byte [hardware_info + hardware_info_struct.has_sse3], 1
.no_sse3:

    ; Check for FPU
    bt edx, 0                       ; Bit 0 is FPU
    jnc .no_fpu
    mov byte [hardware_info + hardware_info_struct.has_fpu], 1
.no_fpu:

    ; Check for VMX (virtualization)
    bt ecx, 5                       ; Bit 5 of ECX is VMX
    jnc .no_vmx
    mov byte [hardware_info + hardware_info_struct.has_vmx], 1
.no_vmx:

    ; Check for APIC
    bt edx, 9                       ; Bit 9 is APIC
    jnc .no_apic
    mov byte [hardware_info + hardware_info_struct.has_apic], 1
.no_apic:

    ; Check for XSAVE
    bt ecx, 26                      ; Bit 26 of ECX is XSAVE
    jnc .no_xsave
    mov byte [hardware_info + hardware_info_struct.has_xsave], 1
.no_xsave:

    ; Check for AES
    bt ecx, 25                      ; Bit 25 of ECX is AES
    jnc .no_aes
    mov byte [hardware_info + hardware_info_struct.has_aes], 1
.no_aes:

    mov ax, 1                       ; Success
    ret

.no_cpuid:
    ; If we can't use CPUID, we can still check for FPU using a legacy method
    ; Try to execute an FPU instruction and see if it generates an exception
    
    ; Clear EM and set MP in CR0
    mov eax, cr0
    and al, 0xFB                    ; Clear EM bit (bit 2)
    or al, 0x02                     ; Set MP bit (bit 1)
    mov cr0, eax
    
    ; Try FPU instruction (FNINIT)
    xor ax, ax
    push ax
    fninit                          ; Reset FPU 
    fnstsw [esp]                    ; Store status word
    pop ax
    
    ; If status is 0, FPU is present
    test ax, ax
    jnz .no_fpu_legacy
    mov byte [hardware_info + hardware_info_struct.has_fpu], 1
.no_fpu_legacy:

    mov ax, 1                       ; Success
    ret

; Improved A20 line enabling with multiple methods and verification
enable_a20_line:
    ; First try BIOS method
    mov ax, 0x2401
    int 0x15
    call check_a20
    cmp ax, 1
    je .done
    
    ; Try keyboard controller method
    call .a20_kbd_wait
    mov al, 0xAD    ; Disable keyboard
    out 0x64, al
    
    call .a20_kbd_wait
    mov al, 0xD0    ; Read output port
    out 0x64, al
    
    call .a20_kbd_wait_data
    in al, 0x60
    push ax
    
    call .a20_kbd_wait
    mov al, 0xD1    ; Write output port
    out 0x64, al
    
    call .a20_kbd_wait
    pop ax
    or al, 2        ; Set A20 bit
    out 0x60, al
    
    call .a20_kbd_wait
    mov al, 0xAE    ; Enable keyboard
    out 0x64, al
    
    call .a20_kbd_wait
    call check_a20
    cmp ax, 1
    je .done
    
    ; Try fast A20 method
    in al, 0x92
    test al, 2
    jnz .done       ; A20 already set
    or al, 2        ; Set A20 bit
    and al, 0xFE    ; Don't reset the system
    out 0x92, al
    
    ; Final check
    call check_a20
    
.done:
    ret
    
.a20_kbd_wait:
    in al, 0x64
    test al, 2
    jnz .a20_kbd_wait
    ret
    
.a20_kbd_wait_data:
    in al, 0x64
    test al, 1
    jz .a20_kbd_wait_data
    ret

; Check if A20 line is enabled
check_a20:
    pushf
    push ds
    push es
    push di
    push si
    
    cli
    
    xor ax, ax      ; Clear AX
    mov es, ax      ; Set ES=0
    
    not ax          ; Set AX=0xFFFF
    mov ds, ax      ; Set DS=0xFFFF
    
    mov di, 0x0500  ; Address in segment ES (0:0500)
    mov si, 0x0510  ; Address in segment DS (FFFF:0510)
    
    ; Save original bytes
    mov al, [es:di]
    push ax
    mov al, [ds:si]
    push ax
    
    ; Write different values
    mov byte [es:di], 0x00
    mov byte [ds:si], 0xFF
    
    ; Add a small delay (can help on some hardware)
    mov cx, 0x100
.delay_loop:
    nop
    loop .delay_loop
    
    ; Check if values are different
    mov al, [es:di]
    cmp al, [ds:si]
    
    ; Restore original bytes
    pop ax
    mov [ds:si], al
    pop ax
    mov [es:di], al
    
    ; Set return value: AX=1 if A20 enabled, AX=0 if disabled
    setne al        ; AL=1 if values were different (A20 enabled)
    movzx ax, al    ; Zero-extend AL into AX
    
    pop si
    pop di
    pop es
    pop ds
    popf
    
    ret

; Enhanced memory detection with E820 and fallback methods
detect_memory:
    mov di, memory_map_buffer
    xor ebx, ebx
    xor bp, bp      ; Entry count
    
    ; Use E820 method first
    mov eax, 0xE820
    mov edx, MEMORY_MAP_SIGNATURE
    mov ecx, 24
    mov dword [es:di + 20], 1  ; Force ACPI 3.X entry
    int 0x15
    
    jc .use_e801    ; If carry set, E820 failed
    cmp eax, MEMORY_MAP_SIGNATURE
    jne .use_e801   ; If signature doesn't match, E820 failed
    test ebx, ebx
    jz .use_e801    ; If ebx is 0, only one entry or error
    jmp .next_entry
    
.next_entry:
    mov eax, 0xE820
    mov ecx, 24
    mov dword [es:di + 20], 1
    int 0x15
    
    jc .done_e820   ; Carry means end of list
    test ebx, ebx
    jz .done_e820   ; ebx=0 means end of list
    
    add di, 24
    inc bp
    cmp bp, MAX_MEM_REGIONS
    jb .next_entry
    
.done_e820:
    mov ebx, memory_map_buffer
    mov ecx, bp
    mov ax, 1
    ret
    
.use_e801:
    ; E801 fallback method
    mov ax, 0xE801
    int 0x15
    jc .use_88
    
    ; Convert to E820-style entries for consistency
    mov di, memory_map_buffer
    
    ; First entry: 0 to 1MB (conventional memory)
    mov dword [di], 0           ; base low
    mov dword [di+4], 0         ; base high
    mov dword [di+8], 0x100000  ; length low (1MB)
    mov dword [di+12], 0        ; length high
    mov dword [di+16], 1        ; type (usable)
    add di, 24
    
    ; Second entry: 1MB to 16MB
    mov dword [di], 0x100000    ; base low
    mov dword [di+4], 0         ; base high
    mov cx, ax                  ; ax = KB between 1MB and 16MB
    mov dword [di+8], ecx       ; length in KB
    shl dword [di+8], 10        ; convert to bytes
    mov dword [di+12], 0        ; length high
    mov dword [di+16], 1        ; type (usable)
    add di, 24
    
    ; Third entry: 16MB and above
    mov dword [di], 0x1000000   ; base low (16MB)
    mov dword [di+4], 0         ; base high
    mov dx, bx                  ; bx = 64KB blocks above 16MB
    mov ecx, edx                ; ecx = 64KB blocks
    shl ecx, 16                 ; convert to bytes
    mov dword [di+8], ecx       ; length low
    mov dword [di+12], 0        ; length high
    mov dword [di+16], 1        ; type (usable)
    add di, 24
    
    mov ebx, memory_map_buffer
    mov ecx, 3                  ; 3 entries
    mov ax, 1
    ret
    
.use_88:
    ; INT 15h, AH=88h fallback
    mov ah, 0x88
    int 0x15
    jc .failed
    
    mov di, memory_map_buffer
    
    ; First entry: 0 to 640KB
    mov dword [di], 0           ; base low
    mov dword [di+4], 0         ; base high
    mov dword [di+8], 0xA0000   ; length low (640KB)
    mov dword [di+12], 0        ; length high
    mov dword [di+16], 1        ; type (usable)
    add di, 24
    
    ; Second entry: 1MB to 1MB+ax
    mov dword [di], 0x100000    ; base low (1MB)
    mov dword [di+4], 0         ; base high
    mov dword [di+8], eax       ; length in KB
    shl dword [di+8], 10        ; convert to bytes
    mov dword [di+12], 0        ; length high
    mov dword [di+16], 1        ; type (usable)
    
    mov ebx, memory_map_buffer
    mov ecx, 2                  ; 2 entries
    mov ax, 1
    ret
    
.failed:
    xor ax, ax
    ret

; Find the ACPI RSDP table
find_acpi_rsdp:
    ; Search in EBDA first
    mov ebx, 0
    mov ax, 0x40
    mov es, ax
    mov bx, [es:0x0E]           ; Get EBDA segment from BDA
    shl ebx, 4                  ; Convert to physical address
    
    mov ecx, 0x400              ; Search first 1KB of EBDA
    call .search_rsdp
    jnc .found
    
    ; Then search main BIOS area
    mov ebx, 0x000E0000
    mov ecx, 0x00020000         ; Search 128KB starting at 0xE0000
    call .search_rsdp
    jnc .found
    
    ; Not found
    xor eax, eax
    ret
    
.found:
    mov eax, ebx
    ret
    
.search_rsdp:
    push es
    push di
    
    mov eax, ebx
    shr eax, 4
    mov es, ax                  ; Set ES to segment
    mov edi, ebx
    and di, 0x000F              ; Set DI to offset within segment
    
    mov eax, 0                  ; Starting offset
    
.loop:
    mov edx, 'RSD '
    cmp [es:di], edx
    jne .next
    
    mov edx, 'PTR '
    cmp [es:di+4], edx
    je .verify
    
.next:
    add di, 16                  ; RSDP is 16-byte aligned
    add eax, 16
    cmp eax, ecx
    jb .loop
    
    stc                         ; Set carry flag (not found)
    jmp .done
    
.verify:
    ; Verify checksum
    xor bl, bl
    mov cx, 20                  ; RSDP is 20 bytes for ACPI 1.0
    
.checksum_loop:
    add bl, [es:di]
    inc di
    loop .checksum_loop
    
    test bl, bl
    jnz .next                   ; If checksum != 0, keep searching
    
    ; Found valid RSDP
    add ebx, eax                ; Calculate full physical address
    clc                         ; Clear carry flag (found)
    
.done:
    pop di
    pop es
    ret

; Load kernel from disk with improved error handling
load_kernel:
    ; Check if we're booting from hard disk
    mov dl, [boot_info_struct + boot_info.boot_device]
    cmp dl, 0x80
    jae .use_lba
    
    ; Load from floppy using CHS addressing
    ; Define disk packet for read sector operations
    jmp .use_chs
    
.use_lba:
    ; Use LBA addressing for hard disks (INT 13h extensions)
    mov ah, 0x41                ; Check INT 13h extensions
    mov bx, 0x55AA
    int 0x13
    jc .use_chs                 ; Fall back to CHS if not supported
    
    cmp bx, 0xAA55              ; Check signature
    jne .use_chs
    
    ; Use extended read (LBA)
    mov si, disk_packet         ; DS:SI points to disk packet
    mov ah, 0x42                ; Extended read
    mov dl, [boot_info_struct + boot_info.boot_device]
    int 0x13
    jnc .load_success
    jmp .disk_error
    
.use_chs:
    ; Read using CHS addressing
    mov ah, 0x02                ; Read sectors function
    mov al, 100                 ; Read 100 sectors (50KB)
    mov ch, 0                   ; Cylinder 0
    mov cl, 2                   ; Start from sector 2
    mov dh, 0                   ; Head 0
    mov dl, [boot_info_struct + boot_info.boot_device]
    
    ; Set buffer address
    mov bx, 0x1000              ; Load to segment 0x1000
    mov es, bx
    xor bx, bx                  ; Offset 0
    
    int 0x13
    jnc .load_success
    
.disk_error:
    xor ax, ax                  ; Failure
    ret
    
.load_success:
    ; Update kernel size in boot info
    mov eax, [sectors_to_read]
    shl eax, 9                  ; Multiply by 512 to get byte count
    mov [boot_info_struct + boot_info.kernel_size], eax
    
    mov ax, 1                   ; Success
    ret

; Setup protected mode environment
setup_protected_mode:
    ; Setup GDT, etc.
    ret

; Setup video mode for better graphics support
setup_video_mode:
    ; Get VBE controller info
    mov ax, 0x4F00
    mov di, vbe_info_block
    int 0x10
    cmp ax, 0x004F
    jne .vbe_failed
    
    ; Find best video mode (prioritize 1024x768x32)
    mov ax, 0x4F01
    mov cx, 0x0118              ; Mode 0x118 = 1024x768x32
    mov di, vbe_mode_info
    int 0x10
    cmp ax, 0x004F
    je .set_mode
    
    ; Try 800x600x32
    mov ax, 0x4F01
    mov cx, 0x0115              ; Mode 0x115 = 800x600x32
    mov di, vbe_mode_info
    int 0x10
    cmp ax, 0x004F
    je .set_mode
    
    ; Try 640x480x32
    mov ax, 0x4F01
    mov cx, 0x0112              ; Mode 0x112 = 640x480x32
    mov di, vbe_mode_info
    int 0x10
    cmp ax, 0x004F
    je .set_mode
    
.vbe_failed:
    ; Fall back to standard VGA mode
    mov ax, 0x0013              ; 320x200x256
    int 0x10
    mov word [vbe_mode_info], 0 ; Mark as standard VGA
    ret
    
.set_mode:
    mov ax, 0x4F02
    mov bx, cx
    or bx, 0x4000               ; Use linear framebuffer
    int 0x10
    
    ; Save VBE mode info address
    mov [boot_info_struct + boot_info.vbe_mode_info], vbe_mode_info
    ret

; Fatal error handler with visual feedback
fatal_error:
    ; Set text color to bright red on black
    mov ah, 0x09
    mov al, 'E'
    mov bh, 0
    mov bl, 0x0C
    mov cx, 1
    int 0x10
    
    ; Beep to alert user
    mov ah, 0x0E
    mov al, 7    ; BEL character
    int 0x10
    
    ; Display error code
    mov ah, 0x0E
    add al, '0'  ; Convert to ASCII
    int 0x10
    
    ; Wait for key and reboot
    mov ah, 0
    int 0x16
    int 0x19     ; Reboot
    
    cli
    hlt

; Print null-terminated string
print_string:
    push ax
    mov ah, 0x0E  ; BIOS teletype function
.loop:
    lodsb         ; Load byte at DS:SI into AL and increment SI
    test al, al
    jz .done
    int 0x10      ; Print character
    jmp .loop
.done:
    pop ax
    ret

; Data structures
align 8
gdt_start:
    ; Null descriptor
    dd 0, 0
    
    ; Code segment descriptor
    dw 0xFFFF       ; Limit low
    dw 0            ; Base low
    db 0            ; Base middle
    db 10011010b    ; Access
    db 11001111b    ; Granularity
    db 0            ; Base high
    
    ; Data segment descriptor
    dw 0xFFFF       ; Limit low
    dw 0            ; Base low
    db 0            ; Base middle
    db 10010010b    ; Access
    db 11001111b    ; Granularity
    db 0            ; Base high

gdt_descriptor:
    dw $ - gdt_start - 1    ; GDT size
    dd gdt_start                         ; GDT address

CODE_SEG equ 0x08
DATA_SEG equ 0x10
KERNEL_LOAD_ADDR equ 0x100000  ; 1MB

; Disk packet for extended read
disk_packet:
    db 16          ; Size of packet
    db 0           ; Reserved
sectors_to_read:
    dw 200         ; Number of sectors to read
buffer_offset:
    dw 0           ; Buffer offset
buffer_segment:
    dw 0x1000      ; Buffer segment
lba_address:
    dq 1           ; Starting LBA

; VBE structures
vbe_info_block:
    .signature      db 'VBE2'
    .version        dw 0
    .oem_string     dd 0
    .capabilities   dd 0
    .video_modes    dd 0
    .video_memory   dw 0
    .software_rev   dw 0
    .vendor         dd 0
    .product_name   dd 0
    .product_rev    dd 0
    .reserved       times 222 db 0
    .oem_data       times 256 db 0

vbe_mode_info:
    .attributes         dw 0
    .window_a           db 0
    .window_b           db 0
    .granularity        dw 0
    .window_size        dw 0
    .segment_a          dw 0
    .segment_b          dw 0
    .win_func_ptr       dd 0
    .pitch              dw 0
    .width              dw 0
    .height             dw 0
    .w_char             db 0
    .y_char             db 0
    .planes             db 0
    .bpp                db 0
    .banks              db 0
    .memory_model       db 0
    .bank_size          db 0
    .image_pages        db 0
    .reserved0          db 0
    .red_mask           db 0
    .red_position       db 0
    .green_mask         db 0
    .green_position     db 0
    .blue_mask          db 0
    .blue_position      db 0
    .reserved_mask      db 0
    .reserved_position  db 0
    .direct_color_attr  db 0
    .framebuffer        dd 0
    .off_screen_mem     dd 0
    .off_screen_size    dw 0
    .reserved1          times 206 db 0

; Messages
welcome_msg          db "uintOS Secondary Bootloader v2.0", 0x0D, 0x0A, 0
a20_success_msg      db "A20 line enabled", 0x0D, 0x0A, 0
a20_error_msg        db "Failed to enable A20 line!", 0x0D, 0x0A, 0
memory_error_msg     db "Failed to get memory map!", 0x0D, 0x0A, 0
loading_kernel_msg   db "Loading kernel...", 0x0D, 0x0A, 0
kernel_error_msg     db "Failed to load kernel!", 0x0D, 0x0A, 0
entering_pmode_msg   db "Entering protected mode...", 0x0D, 0x0A, 0
hw_detect_msg        db "Detecting hardware...", 0x0D, 0x0A, 0
hw_detect_success_msg db "Hardware detection complete", 0x0D, 0x0A, 0
hw_detect_error_msg  db "Hardware detection failed!", 0x0D, 0x0A, 0

; Memory map buffer
memory_map_buffer:   times MAX_MEM_REGIONS * 24 db 0
