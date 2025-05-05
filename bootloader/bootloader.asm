;*********************************************
; PrimaryBoot.asm
; A uintOS Bootloader for Educational Purposes
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
%define MAX_RETRIES 3                ; Maximum retries for disk operations

; Memory map entry structure
struc mem_map_entry
    .base_addr_low:  resd 1
    .base_addr_high: resd 1
    .length_low:     resd 1
    .length_high:    resd 1
    .type:           resd 1
    .acpi_attr:      resd 1
endstruc

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
welcome_message db "Welcome to uintOS!", 0ah, 0dh, 0h
disk_error_msg db "Disk read error! Retrying...", 0ah, 0dh, 0h
disk_fatal_msg db "FATAL: Could not read disk after multiple attempts.", 0ah, 0dh, 0h
a20_enabled_msg db "A20 line enabled successfully.", 0ah, 0dh, 0h
a20_failed_msg db "WARNING: Failed to enable A20 line.", 0ah, 0dh, 0h
mem_detect_msg db "Detecting system memory...", 0ah, 0dh, 0h
mem_complete_msg db "Memory detection complete.", 0ah, 0dh, 0h
loading_kernel_msg db "Loading kernel...", 0ah, 0dh, 0h
switching_pm_msg db "Switching to protected mode...", 0ah, 0dh, 0h

cursor_x_pos db 0
cursor_y_pos db 0
retry_count db 0                    ; Counter for disk read retries

; Memory info storage
mem_map_buffer times 20 * 24 db 0   ; Buffer for memory map (20 entries max)
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
    
    sti                             ; Enable interrupts
    
    ; Print welcome message
    mov si, welcome_message
    call print_message
    
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
    mov esp, 0x10000                ; Set stack pointer to a safe location
    
    ; Load TSS
    mov ax, 0x18                    ; TSS selector (0x18)
    ltr ax
    
    ; Jump to kernel entry point
    jmp 0x1000                      ; Jump to the loaded kernel entry point

    ; Should never reach here
    cli
    hlt

;-----------------------------------------------
; load_kernel - Loads the kernel from the disk
;-----------------------------------------------
load_kernel:
    xor cx, cx                      ; Reset retry counter
    mov [retry_count], cl

.retry:
    ; Reset disk system
    mov ah, 0
    mov dl, [DriveNumber]
    int 0x13
    jc .disk_error
    
    ; First read
    mov ax, LOAD_SEGMENT
    mov es, ax
    xor bx, bx                      ; Set buffer to ES:BX
    
    mov SECTOR_COUNT, 17            ; Read 17 sectors (8.5KB)
    mov CYLINDER_NUM, 0             ; Cylinder 0
    mov SECTOR_NUM, 2               ; Start from sector 2 (after boot sector)
    mov HEAD_NUM, 0                 ; Head 0
    mov DRIVE_NUM, [DriveNumber]    ; Use stored drive number
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
    mov DRIVE_NUM, [DriveNumber]    ; Use stored drive number
    mov ah, 0x02                    ; Read sectors function
    int 0x13
    jc .disk_error
    
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
; enable_a20 - Enables the A20 line
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
    mov dl, [ds:si]
    mov dh, [es:di]
    
    ; Write different values to test
    mov byte [ds:si], 0x00
    mov byte [es:di], 0xFF
    
    ; Check if addresses wrapped around (A20 disabled) or separate (A20 enabled)
    mov al, [ds:si]
    cmp al, 0xFF      ; If A20 is disabled, writing to FFFF:0510 affects 0000:0500
    
    ; Restore original values
    mov [ds:si], dl
    mov [es:di], dh
    
    mov ax, 0         ; Assume not enabled
    jz .check_done    ; If they're equal, A20 is disabled
    mov ax, 1         ; A20 is enabled
    
.check_done:
    sti               ; Restore interrupts
    pop si
    pop di
    pop es
    pop ds
    popf
    ret

;-----------------------------------------------
; detect_memory - Detects available memory using INT 15h
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
    jc .done                        ; Carry set = error/finished
    
    cmp eax, 0x534D4150             ; Check signature in EAX
    jne .done                       ; If not matching, we're done
    
    test ebx, ebx                   ; Is continuation value 0?
    jz .done                        ; If yes, we're done
    
    inc bp                          ; Increment entry count
    add di, 24                      ; Point to next entry
    
    cmp bp, 20                      ; Check if we've reached our max
    jb .next_entry                  ; If not, continue
    
.done:
    mov [mem_map_entries], bp       ; Store the entry count
    ret

;-----------------------------------------------
; move_cursor - Moves the cursor to the position stored in cursor_x_pos/y_pos
;-----------------------------------------------
move_cursor:
    mov bh, 0                       ; Page number
    mov dl, [cursor_x_pos]          ; Column
    mov dh, [cursor_y_pos]          ; Row
    mov ah, 2                       ; Set cursor position function
    int 10h
    ret

;-----------------------------------------------
; print_message - Prints a null-terminated string pointed to by SI
;-----------------------------------------------
print_message:
.loop:
    lodsb                           ; Load byte from SI into AL
    or al, al                       ; Check if AL is 0 (end of string)
    jz .done                        ; If zero, we're done
    mov cx, 1                       ; Print 1 character
    call print_char                 ; Call print_char
    jmp .loop                       ; Continue with next character
.done:
    ret

;-----------------------------------------------
; print_char - Prints the character in AL at the current cursor position
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
        at trap, dw 0x00           ; Not used
        at io_base, dw 0x00        ; No IO permission bitmap
    iend

;-----------------------------------------------
; GDT - Global Descriptor Table
;-----------------------------------------------
gdt_entries:
    ; Null descriptor
    dd 0
    dd 0

    ; Code segment descriptor (0x08)
    dw 0xFFFF                       ; Limit (bits 0-15)
    dw 0x0000                       ; Base (bits 0-15)
    db 0x00                         ; Base (bits 16-23)
    db 10011010b                    ; Access byte (Present, Ring 0, Code)
    db 11001111b                    ; Flags and limit (bits 16-19)
    db 0x00                         ; Base (bits 24-31)

    ; Data segment descriptor (0x10)
    dw 0xFFFF                       ; Limit (bits 0-15)
    dw 0x0000                       ; Base (bits 0-15)
    db 0x00                         ; Base (bits 16-23)
    db 10010010b                    ; Access byte (Present, Ring 0, Data)
    db 11001111b                    ; Flags and limit (bits 16-19)
    db 0x00                         ; Base (bits 24-31)

    ; TSS descriptor (0x18)
    dw (init_tss_end - init_tss) - 1 ; Limit (size of TSS)
    dw init_tss                     ; Base (offset of TSS)
    db 0x00                         ; Base (bits 16-23)
    db 10001001b                    ; Access byte (Present, Ring 0, TSS)
    db 00000000b                    ; Flags and limit
    db 0x00                         ; Base (bits 24-31)

init_tss_end:                       ; Mark the end of TSS for size calculation

gdt_descriptor:
    dw gdt_entries_end - gdt_entries - 1  ; GDT size - 1
    dd gdt_entries                        ; GDT address

gdt_entries_end:

; Pad to 510 bytes and add boot signature
times 510 - ($-$$) db 0
dw 0xAA55                           ; Boot signature
