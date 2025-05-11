;*********************************************
; PrimaryBoot.asm
; A uintOS Bootloader for Educational Purposes
; Enhanced for real hardware boot compatibility
;*********************************************
bits 16

boot_entry: jmp boot_start

; BIOS Parameter Block to ensure compatibility with various BIOSes
; We're adding a proper BPB to avoid issues with some BIOS implementations
; that overwrite the boot sector if a valid BPB is not found
OEMLabel            db "uintOS   "   ; OEM Label (8 bytes)
BytesPerSector      dw 512           ; Bytes per sector
SectorsPerCluster   db 1             ; Sectors per cluster
ReservedSectors     dw 1             ; Reserved sectors
NumberOfFATs        db 2             ; Number of FATs
RootEntries         dw 224           ; Root directory entries
TotalSectors        dw 2880          ; Total sectors (for 1.44MB floppy)
MediaDescriptor     db 0xF0          ; Media descriptor (0xF0 for floppy)
SectorsPerFAT       dw 9             ; Sectors per FAT
SectorsPerTrack     dw 18            ; Sectors per track
HeadsPerCylinder    dw 2             ; Number of heads
HiddenSectors       dd 0             ; Hidden sectors
LargeSectors        dd 0             ; Large total sectors
DriveNumber         db 0             ; Drive number
                    db 0             ; Reserved
ExtendedBootSig     db 0x29          ; Extended boot signature
VolumeID            dd 0xC0FFEE42    ; Volume ID
VolumeLabel         db "UINT OS    " ; Volume label (11 bytes)
FileSystem          db "FAT12   "    ; File system type (8 bytes)

%define SECTOR_COUNT al
%define CYLINDER_NUM ch
%define SECTOR_NUM cl
%define HEAD_NUM dh
%define DRIVE_NUM dl

%define LOAD_ADDRESS 1000h
%define LOAD_SEGMENT 100h
%define MAX_RETRIES 5                ; Increased retries for disk operations

; Memory map entry structure
struc mem_map_entry
    .base_addr_low:  resd 1
    .base_addr_high: resd 1
    .length_low:     resd 1
    .length_high:    resd 1
    .type:           resd 1
    .acpi_extended:  resd 1
endstruc

; TSS Structure for protected mode
struc tss_struct
  link: resw 1
  esp0: resd 1
  ss0: resw 1
  esp1: resd 1
  ss1: resw 1
  esp2: resd 1
  ss2: resw 1
  cr3: resd 1
  eip: resd 1
  eflags: resd 1
  eax: resd 1
  ecx: resd 1
  edx: resd 1
  ebx: resd 1
  esp: resd 1
  ebp: resd 1
  esi: resd 1
  edi: resd 1
  es: resw 1
  cs: resw 1
  ss: resw 1
  ds: resw 1
  fs: resw 1
  gs: resw 1
  ldt: resw 1
  trap: resw 1
  io_base: resw 1
endstruc

; Messages
welcome_message db "Welcome to uintOS Bootloader v2.0 - Real Hardware Edition!", 0ah, 0dh, 0h
disk_error_msg db "Disk read error! Retrying...", 0ah, 0dh, 0h
disk_fatal_msg db "FATAL: Could not read disk after multiple attempts.", 0ah, 0dh, 0h
a20_enabled_msg db "A20 line enabled successfully.", 0ah, 0dh, 0h
a20_failed_msg db "WARNING: Failed to enable A20 line.", 0ah, 0dh, 0h
mem_detect_msg db "Detecting system memory...", 0ah, 0dh, 0h
mem_complete_msg db "Memory detection complete.", 0ah, 0dh, 0h
loading_kernel_msg db "Loading kernel...", 0ah, 0dh, 0h
switching_pm_msg db "Switching to protected mode...", 0ah, 0dh, 0h
hardware_detect_msg db "Detecting hardware...", 0ah, 0dh, 0h
loading_from_msg db "Loading from drive 0x", 0h
drive_type_floppy db "Floppy disk", 0ah, 0dh, 0h
drive_type_harddisk db "Hard disk", 0ah, 0dh, 0h
drive_type_unknown db "Unknown media", 0ah, 0dh, 0h

cursor_x_pos db 0
cursor_y_pos db 0
retry_count db 0                    ; Counter for disk read retries
boot_drive db 0                     ; Store boot drive number

; Memory info storage
mem_map_buffer times 24 * 32 db 0   ; Buffer for memory map (32 entries max)
mem_map_entries dw 0                ; Number of memory map entries found

boot_start:
    cli                             ; Clear interrupts during setup
    cld                             ; Clear direction flag
    
    ; Set up segment registers and stack
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00                  ; Set stack below bootloader
    
    ; Save boot drive number
    mov [boot_drive], dl
    
    sti                             ; Enable interrupts
    
    ; Print welcome message
    mov si, welcome_message
    call print_message
    
    ; Display drive information
    mov si, loading_from_msg
    call print_message
    
    mov al, [boot_drive]
    call print_hex_byte
    
    ; Identify drive type
    mov dl, [boot_drive]
    cmp dl, 0x80                    ; Is it >= 0x80 (hard disk)?
    jb .floppy_drive
    
    ; Hard disk
    mov si, drive_type_harddisk
    jmp .print_drive_type
    
.floppy_drive:
    mov si, drive_type_floppy
    
.print_drive_type:
    call print_message
    
    ; Hardware detection
    mov si, hardware_detect_msg
    call print_message
    call detect_hardware
    
    ; Enable A20 line for full memory access
    call enable_a20
    
    ; Detect memory
    mov si, mem_detect_msg
    call print_message
    call detect_memory
    
    mov si, mem_complete_msg
    call print_message

    ; Load the kernel
    mov si, loading_kernel_msg
    call print_message
    call load_kernel
    
    ; Prepare for protected mode
    mov si, switching_pm_msg
    call print_message
    
    ; Set up GDT and switch to protected mode
    call setup_gdt
    
    ; Switch to protected mode
    cli                             ; Disable interrupts
    mov eax, cr0                    ; Get current CR0
    or eax, 1                       ; Set PE bit (bit 0)
    mov cr0, eax                    ; Enter protected mode
    
    ; Far jump to clear the instruction pipeline and load CS with 32-bit selector
    jmp 0x08:protected_mode_entry   ; 0x08 is the code segment selector

; 32-bit protected mode code starts here
[BITS 32]
protected_mode_entry:
    ; Set up segment registers for protected mode
    mov ax, 0x10                    ; Data segment selector (0x10)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Set up stack
    mov esp, 0x90000                ; Set stack pointer to a safe location
    
    ; Load TSS
    mov ax, 0x18                    ; TSS selector (0x18)
    ltr ax
    
    ; Jump to kernel entry point with boot information
    mov eax, [mem_map_entries]      ; Pass number of memory map entries 
    mov ebx, mem_map_buffer         ; Pass memory map buffer address
    mov ecx, [boot_drive]           ; Pass boot drive number
    
    jmp 0x1000                      ; Jump to the loaded kernel entry point

    ; Should never reach here
    cli
    hlt

; Detect hardware capabilities and features
detect_hardware:
    pusha
    
    ; Check for CPUID support by attempting to flip bit 21 of EFLAGS
    ; (ID flag - indicates CPUID instruction support)
    pushfd
    pop eax                ; Get EFLAGS
    mov ecx, eax           ; Save original EFLAGS
    xor eax, 0x00200000    ; Flip ID bit
    push eax
    popfd                  ; Load modified EFLAGS
    pushfd
    pop eax                ; Get new EFLAGS
    push ecx
    popfd                  ; Restore original EFLAGS
    xor eax, ecx           ; See if bit changed
    jz .no_cpuid           ; If not, CPUID not supported
    
    ; Get CPU vendor string
    xor eax, eax           ; EAX = 0: Get vendor ID
    cpuid
    push eax               ; Save EAX
    
    ; EBX, EDX, ECX now contain vendor string
    
    ; Get processor features
    mov eax, 1             ; EAX = 1: Get processor info and feature bits
    cpuid
    
    ; EDX now contains feature flags
    ; For example, check for SSE support (bit 25)
    test edx, (1 << 25)    ; Test bit 25 (SSE)
    jz .no_sse
    
    ; SSE is supported
    
.no_sse:
    pop eax                ; Restore EAX
    
.no_cpuid:
    ; Check other hardware features as needed
    ; ...
    
    popa
    ret

;-----------------------------------------------
; load_kernel - Loads the kernel from the disk
;-----------------------------------------------
load_kernel:
    xor cx, cx                      ; Reset retry counter
    mov [retry_count], cl

.retry:
    ; Check if we're loading from floppy or hard disk
    cmp byte [boot_drive], 0x80
    jb .load_from_floppy
    
    ; Load from hard disk using LBA
    call load_kernel_lba
    jnc .load_success                ; If carry clear, load was successful
    jmp .disk_error
    
.load_from_floppy:
    ; Reset disk system for floppy
    mov ah, 0
    mov dl, [boot_drive]
    int 0x13
    jc .disk_error
    
    ; First read for floppy
    mov ax, LOAD_SEGMENT
    mov es, ax
    xor bx, bx                      ; Set buffer to ES:BX
    
    mov SECTOR_COUNT, 17            ; Read 17 sectors (8.5KB)
    mov CYLINDER_NUM, 0             ; Cylinder 0
    mov SECTOR_NUM, 2               ; Start from sector 2 (after boot sector)
    mov HEAD_NUM, 0                 ; Head 0
    mov DRIVE_NUM, [boot_drive]     ; Use stored drive number
    mov ah, 0x02                    ; Read sectors function
    int 0x13
    jc .disk_error
    
    ; Second read - continue loading
    mov ax, LOAD_SEGMENT
    mov es, ax
    mov bx, 512*17                  ; Offset to next loading position
    
    mov SECTOR_COUNT, 18            ; Read 18 sectors (9KB)
    mov CYLINDER_NUM, 0             ; Cylinder 0
    mov SECTOR_NUM, 1               ; Start from sector 1
    mov HEAD_NUM, 1                 ; Head 1
    mov DRIVE_NUM, [boot_drive]     ; Use stored drive number
    mov ah, 0x02                    ; Read sectors function
    int 0x13
    jc .disk_error
    
.load_success:
    ret                             ; Success

.disk_error:
    ; Handle disk read error
    mov si, disk_error_msg
    call print_message
    
    inc byte [retry_count]
    cmp byte [retry_count], MAX_RETRIES
    jb .retry                       ; Try again if under max retries
    
    ; Fatal error after max retries
    mov si, disk_fatal_msg
    call print_message
    
    ; Wait for key press and reboot
    xor ah, ah
    int 0x16                        ; Wait for keypress
    int 0x19                        ; Reboot
    
    cli                             ; Should never get here
    hlt

;-----------------------------------------------
; load_kernel_lba - Load kernel using LBA for hard disks
;-----------------------------------------------
load_kernel_lba:
    ; Use Disk Address Packet (DAP) method for LBA reads
    push ds
    push si
    
    ; Create DAP on stack
    push word 0         ; Offset 10-11: high word of transfer buffer
    push LOAD_SEGMENT   ; Offset 8-9: segment of transfer buffer
    push word 0         ; Offset 6-7: high word of starting LBA
    push word 1         ; Offset 4-5: starting LBA 1 (skip MBR)
    push word 30        ; Offset 2-3: sectors to read
    push word 16        ; Offset 0-1: size of DAP (16 bytes)
    
    mov si, sp          ; DS:SI points to DAP
    mov ah, 0x42        ; Extended read
    mov dl, [boot_drive]
    int 0x13            ; Call BIOS
    
    ; Clean up stack
    add sp, 12          ; Remove DAP from stack
    
    pop si
    pop ds
    ret

;-----------------------------------------------
; enable_a20 - Enables the A20 line using multiple methods
;-----------------------------------------------
enable_a20:
    ; Try BIOS method first
    mov ax, 0x2401
    int 0x15
    jnc .a20_enabled                ; Carry clear = success
    
    ; Try keyboard controller method
    call .a20_wait
    mov al, 0xAD                    ; Disable keyboard
    out 0x64, al
    
    call .a20_wait
    mov al, 0xD0                    ; Read output port
    out 0x64, al
    
    call .a20_wait_input
    in al, 0x60                     ; Get output port data
    push ax                         ; Save output port data
    
    call .a20_wait
    mov al, 0xD1                    ; Write to output port
    out 0x64, al
    
    call .a20_wait
    pop ax                          ; Restore output port data
    or al, 2                        ; Set bit 1 (A20)
    out 0x60, al
    
    call .a20_wait
    mov al, 0xAE                    ; Enable keyboard
    out 0x64, al
    
    call .a20_wait
    
    ; Verify A20 is enabled
    call .check_a20
    cmp ax, 1
    je .a20_enabled
    
    ; Try fast A20 method as last resort
    in al, 0x92
    or al, 2
    out 0x92, al
    
    ; Final verification
    call .check_a20
    cmp ax, 1
    je .a20_enabled
    
    ; Failed to enable A20
    mov si, a20_failed_msg
    call print_message
    ret
    
.a20_enabled:
    mov si, a20_enabled_msg
    call print_message
    ret

.a20_wait:
    in al, 0x64                     ; Read status
    test al, 2                      ; Test bit 1 (input buffer status)
    jnz .a20_wait                   ; If not zero, controller is busy
    ret

.a20_wait_input:
    in al, 0x64                     ; Read status
    test al, 1                      ; Test bit 0 (output buffer status)
    jz .a20_wait_input              ; If zero, no output available
    ret

.check_a20:
    pushf
    push ds
    push es
    push di
    push si
    
    cli                             ; Clear interrupts
    
    ; Set DS:SI = 0000:0500
    xor ax, ax
    mov ds, ax
    mov si, 0x0500
    
    ; Set ES:DI = FFFF:0510
    mov ax, 0xFFFF
    mov es, ax
    mov di, 0x0510
    
    ; Save original values at these addresses
    mov al, [ds:si]
    push ax
    mov al, [es:di]
    push ax
    
    ; Write different values to test if they wrap around
    mov byte [ds:si], 0x00
    mov byte [es:di], 0xFF
    
    ; Let CPU execute pending operations
    wbinvd
    
    ; Check if values are different (A20 enabled)
    mov al, [ds:si]
    cmp al, 0x00
    jne .check_a20_different
    
    mov al, [es:di]
    cmp al, 0xFF
    jne .check_a20_different
    
    ; If we're here, values are the same (A20 disabled)
    mov ax, 0
    jmp .check_a20_done
    
.check_a20_different:
    ; Values are different, A20 is enabled
    mov ax, 1
    
.check_a20_done:
    ; Restore original values
    pop bx
    mov byte [es:di], bl
    pop bx
    mov byte [ds:si], bl
    
    ; Restore registers
    pop si
    pop di
    pop es
    pop ds
    popf
    ret

;-----------------------------------------------
; detect_memory - Detect available memory
;-----------------------------------------------
detect_memory:
    ; Use INT 15h, AX=E820h BIOS function
    xor ebx, ebx                    ; Clear continuation value
    mov di, mem_map_buffer          ; Point to buffer
    xor bp, bp                      ; Entry count
    
.next_entry:
    mov eax, 0xE820                 ; E820h - Query System Address Map
    mov edx, 0x534D4150             ; "SMAP" signature
    mov ecx, 24                     ; Ask for 24 bytes
    int 0x15
    jc .e820_done                   ; Carry set = error/finished
    
    cmp eax, 0x534D4150             ; Check signature in EAX
    jne .e820_done                  ; If not matching, we're done
    
    test ebx, ebx                   ; Is continuation value 0?
    jz .e820_done                   ; If yes, we're done
    
    ; Filter out non-usable regions
    mov eax, [di + 16]              ; Get memory type
    test eax, eax                   ; Type 0 = not usable
    jz .skip_entry
    
    inc bp                          ; Increment entry count
    add di, 24                      ; Point to next entry
    
    cmp bp, 32                      ; Check if we've reached our max
    jb .next_entry                  ; If not, continue
    jmp .e820_done
    
.skip_entry:
    add di, 24                      ; Skip to next entry
    jmp .next_entry
    
.e820_done:
    ; Save the number of entries
    mov [mem_map_entries], bp
    
    ; If E820 failed, use alternate methods
    test bp, bp
    jnz .detection_done
    
    ; Try E801h method
    mov ax, 0xE801
    int 0x15
    jnc .use_e801
    
    ; Last resort - use 88h
    mov ah, 0x88
    int 0x15
    jc .detection_done              ; If even this fails, we're done
    
    ; Convert 88h result (AX = KB above 1MB) to an e820-like entry
    mov di, mem_map_buffer
    
    ; First entry: 0 to 640KB conventional memory
    mov dword [di], 0               ; Base low
    mov dword [di+4], 0             ; Base high
    mov dword [di+8], 0xA0000       ; Length low (640KB)
    mov dword [di+12], 0            ; Length high
    mov dword [di+16], 1            ; Type (usable)
    add di, 24
    
    ; Second entry: 1MB to (1MB + AX)
    mov dword [di], 0x00100000      ; Base low (1MB)
    mov dword [di+4], 0             ; Base high
    movzx ecx, ax                   ; AX = KB above 1MB
    shl ecx, 10                     ; Convert to bytes
    mov [di+8], ecx                 ; Length low
    mov dword [di+12], 0            ; Length high
    mov dword [di+16], 1            ; Type (usable)
    
    mov byte [mem_map_entries], 2   ; We have 2 entries
    jmp .detection_done
    
.use_e801:
    ; Convert E801 result to e820-like entries
    ; AX = KB between 1MB and 16MB, BX = 64KB blocks above 16MB
    mov di, mem_map_buffer
    
    ; First entry: 0 to 640KB conventional memory
    mov dword [di], 0               ; Base low
    mov dword [di+4], 0             ; Base high
    mov dword [di+8], 0xA0000       ; Length low (640KB)
    mov dword [di+12], 0            ; Length high
    mov dword [di+16], 1            ; Type (usable)
    add di, 24
    
    ; Second entry: 1MB to 16MB
    mov dword [di], 0x00100000      ; Base low (1MB)
    mov dword [di+4], 0             ; Base high
    movzx ecx, ax                   ; AX = KB between 1MB and 16MB
    shl ecx, 10                     ; Convert to bytes
    mov [di+8], ecx                 ; Length low
    mov dword [di+12], 0            ; Length high
    mov dword [di+16], 1            ; Type (usable)
    add di, 24
    
    ; Third entry: 16MB and above
    mov dword [di], 0x01000000      ; Base low (16MB)
    mov dword [di+4], 0             ; Base high
    movzx ecx, bx                   ; BX = 64KB blocks above 16MB
    shl ecx, 16                     ; Convert to bytes
    mov [di+8], ecx                 ; Length low
    mov dword [di+12], 0            ; Length high
    mov dword [di+16], 1            ; Type (usable)
    
    mov byte [mem_map_entries], 3   ; We have 3 entries
    
.detection_done:
    ret

;-----------------------------------------------
; print_message - Print a null-terminated message
;-----------------------------------------------
print_message:
    push ax
    push bx
    
.next_char:
    lodsb                           ; Load next character
    test al, al                     ; Check for null terminator
    jz .done
    
    mov ah, 0x0E                    ; Teletype output
    mov bx, 0x0007                  ; Page 0, text attribute
    int 0x10                        ; Call BIOS
    jmp .next_char
    
.done:
    pop bx
    pop ax
    ret

;-----------------------------------------------
; print_hex_byte - Print a byte in hexadecimal
;-----------------------------------------------
print_hex_byte:
    push ax
    push cx
    
    mov cl, al                      ; Save the byte
    mov al, cl
    shr al, 4                       ; Get high nibble
    call .print_nibble
    
    mov al, cl
    and al, 0x0F                    ; Get low nibble
    call .print_nibble
    
    pop cx
    pop ax
    ret
    
.print_nibble:
    and al, 0x0F                    ; Ensure only nibble
    add al, '0'                     ; Convert to ASCII
    cmp al, '9'
    jbe .print_digit
    add al, 7                       ; Adjust for A-F
    
.print_digit:
    mov ah, 0x0E                    ; Teletype output
    mov bx, 0x0007                  ; Page 0, text attribute
    int 0x10                        ; Call BIOS
    ret

;-----------------------------------------------
; move_cursor - Move cursor to position stored in variables
;-----------------------------------------------
move_cursor:
    pusha
    mov ah, 02h                     ; Set cursor position function
    mov bh, 0                       ; Page number
    mov dh, [cursor_y_pos]          ; Row
    mov dl, [cursor_x_pos]          ; Column
    int 10h
    popa
    ret

;-----------------------------------------------
; print_char - Print a character and update cursor
;-----------------------------------------------
print_char:
    mov bh, 0                       ; Page number
    mov ah, 0ah                     ; Write character function
    int 10h
    
    add [cursor_x_pos], cx          ; Update X position
    
    ; Check if we need to move to the next line
    cmp byte [cursor_x_pos], 80
    jb .move_cursor                 ; If less than 80, just move the cursor
    
    ; Move to the next line
    mov byte [cursor_x_pos], 0
    inc byte [cursor_y_pos]
    
    ; Check if we need to scroll
    cmp byte [cursor_y_pos], 25
    jb .move_cursor                 ; If less than 25, just move the cursor
    
    ; Scroll the screen up one line
    dec byte [cursor_y_pos]         ; Move back to the last line
    
    ; Use BIOS to scroll
    mov ah, 06h                     ; Scroll up function
    mov al, 1                       ; Scroll one line
    mov bh, 07h                     ; Normal attribute
    mov cx, 0                       ; Upper left corner (0,0)
    mov dh, 24                      ; Lower right corner row
    mov dl, 79                      ; Lower right corner column
    int 10h
    
.move_cursor:
    call move_cursor                ; Update cursor position
    ret

;-----------------------------------------------
; setup_gdt - Sets up the Global Descriptor Table for protected mode
;-----------------------------------------------
setup_gdt:
    cli
    pusha
    lgdt [gdt_descriptor]
    sti
    popa
    ret

;-----------------------------------------------
; TSS structure - Initial Task State Segment
;-----------------------------------------------
init_tss:
    istruc tss_struct
        at link, dw 0
        at esp0, dd 0x10000         ; Initial kernel stack
        at ss0, dw 0x10            ; Kernel data segment
        at esp1, dd 0x10000
        at ss1, dw 0x10
        at esp2, dd 0x10000
        at ss2, dw 0x10
        at cr3, dd 0x2000          ; Page directory physical address
        at eip, dd 0x1000          ; Entry point address
        at eflags, dd 0x202        ; Interrupts enabled (IF=1)
        at eax, dd 0
        at ecx, dd 0
        at edx, dd 0
        at ebx, dd 0
        at esp, dd 0x10000
        at ebp, dd 0
        at esi, dd 0
        at edi, dd 0
        at es, dw 0x10             ; Data segment
        at cs, dw 0x8              ; Code segment
        at ss, dw 0x10             ; Data segment for stack
        at ds, dw 0x10             ; Data segment
        at fs, dw 0x10             ; Data segment
        at gs, dw 0x10             ; Data segment
        at ldt, dw 0x00            ; No LDT
        at trap, dw 0
        at io_base, dw 0
    iend

;-----------------------------------------------
; GDT - Global Descriptor Table
;-----------------------------------------------
gdt_start:
    ; Null descriptor (required)
    dd 0                            ; Low dword
    dd 0                            ; High dword
    
    ; Code segment descriptor
    dw 0xFFFF                       ; Limit low (0-15)
    dw 0                            ; Base low (0-15)
    db 0                            ; Base middle (16-23)
    db 10011010b                    ; Access byte: Present, Ring 0, Code segment, Executable, Direction 0, Readable
    db 11001111b                    ; Flags & Limit high: 32-bit, 4KB granularity, Limit (16-19)
    db 0                            ; Base high (24-31)
    
    ; Data segment descriptor
    dw 0xFFFF                       ; Limit low (0-15)
    dw 0                            ; Base low (0-15)
    db 0                            ; Base middle (16-23)
    db 10010010b                    ; Access byte: Present, Ring 0, Data segment, Writable
    db 11001111b                    ; Flags & Limit high: 32-bit, 4KB granularity, Limit (16-19)
    db 0                            ; Base high (24-31)
    
    ; TSS descriptor
    dw init_tss_end - init_tss - 1  ; Limit (size of TSS)
    dw init_tss                     ; Base low
    db 0                            ; Base middle
    db 10001001b                    ; Access byte: Present, Ring 0, System segment, 32-bit TSS (Available)
    db 00000000b                    ; Flags & Limit high: No flags, byte granularity, upper limit is 0
    db 0                            ; Base high
gdt_entries_end:

; GDT descriptor
gdt_descriptor:
    dw gdt_entries_end - gdt_start - 1  ; GDT size (limit)
    dd gdt_entries                        ; GDT address

gdt_entries:

; The fn wbinvd is an x86 instruction (Write Back and Invalidate Cache) 
; If your processor/assembler doesn't support it, here's a safer 
; byte-code implementation that should work on older processors
wbinvd:
    ; CPU sync instruction equivalent
    jmp $+2      ; Short delay
    ret

; Pad to 510 bytes and add boot signature
times 510 - ($-$$) db 0
dw 0xAA55                           ; Boot signature
