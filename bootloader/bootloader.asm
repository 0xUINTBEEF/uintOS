;*********************************************
; PrimaryBoot.asm
; A uintOS Bootloader for Educational Purposes
;*********************************************
bits 16

boot_entry: jmp boot_start

%define SECTOR_COUNT al
%define CYLINDER_NUM ch
%define SECTOR_NUM cl
%define HEAD_NUM dh
%define DRIVE_NUM dl

%define LOAD_ADDRESS 1000h
%define LOAD_SEGMENT 100h

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

welcome_message db "Welcome to uintOS!", 0ah, 0dh, 0h
cursor_x_pos db 0
cursor_y_pos db 0

boot_start:
  cli
  cld

  mov si, welcome_message
  call print_message

  mov ah, 0
  mov dl, 0
  int 0x13
  jc boot_start

  mov ax, LOAD_SEGMENT
  mov es, ax
  xor bx, bx

  mov SECTOR_COUNT, 17
  mov CYLINDER_NUM, 0
  mov SECTOR_NUM, 2
  mov HEAD_NUM, 0
  mov DRIVE_NUM, 0
  mov ah, 0x02
  int 0x13

  mov ax, LOAD_SEGMENT
  mov es, ax
  mov bx, 512*17

  mov SECTOR_COUNT, 18
  mov CYLINDER_NUM, 0
  mov SECTOR_NUM, 1
  mov HEAD_NUM, 1
  mov DRIVE_NUM, 0
  mov ah, 0x02
  int 0x13

  call setup_gdt

  mov eax, cr0
  or eax, 1
  mov cr0, eax
  jmp clear_cache

clear_cache:
  mov eax, 0x10
  mov ss, [init_tss + ss]
  mov esp, [init_tss + esp0]
  push dword [init_tss + eflags]
  push dword [init_tss + cs]
  push dword [init_tss + eip]
  call 0x18:00

  cli
  hlt

move_cursor:
  mov bh, 0
  mov ah, 2
  int 10h

  mov [cursor_x_pos], dl
  mov [cursor_y_pos], dh
  ret

print_message:
.loop:
  lodsb
  or al, al
  jz .done
  mov cx, 1
  call print_char
  jmp .loop
.done:
  ret

print_char:
  mov bh, 0
  mov ah, 0ah
  int 10h

  add [cursor_x_pos], cx
  mov dl, [cursor_x_pos]
  mov dh, [cursor_y_pos]
  call move_cursor

  ret

setup_gdt:
  cli
  pusha
  lgdt [gdt_descriptor]
  sti
  popa
  ret

init_tss:
  istruc tss_struct
    at link, dw 0
    at esp0, dd 0x10000
    at ss0, dd 0x10
    at esp1, dd 0x10000
    at ss1, dd 0x10
    at esp2, dd 0x10000
    at ss2, dd 0x10
    at cr3, dd 0x2000
    at eip, dd 0x45
    at esp, dd 0x10000
    at es, dd 0x10
    at cs, dd 0x8
    at ss, dd 0x10
    at ds, dd 0x10
    at fs, dd 0x10
    at gs, dd 0x10
    at ldt, dw 0x00
    at trap, dw 0x00
    at io_base, dw 0x00
  iend

gdt_entries:
  dd 0
  dd 0

  dw 0FFFFh
  dw 0x0000
  db 0x00
  db 10011110b
  db 11001111b
  db 0x00

  dw 0FFFFh
  dw 0x0000
  db 0x00
  db 10010010b
  db 11001111b
  db 0

  dw 0x067
  dw init_tss
  db 0x00
  db 10001001b
  db 00010000b
  db 0x00

gdt_descriptor:
  dw gdt_entries - gdt_entries - 1
  dd gdt_entries

times 510 - ($-$$) db 0

dw 0xAA55
