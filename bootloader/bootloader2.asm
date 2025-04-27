;*********************************************
; SecondaryBoot.asm
; A uintOS Secondary Bootloader
;*********************************************
bits 16

secondary_entry: jmp secondary_start

%define SECTOR_TOTAL al
%define CYLINDER_INDEX ch
%define SECTOR_INDEX cl
%define HEAD_INDEX dh
%define DRIVE_INDEX dl

%define LOAD_MEMORY 1000h
%define LOAD_SEGMENT 100h

welcome_message db "Loading uintOS...", 0ah, 0dh, 0h
cursor_x_pos db 0
cursor_y_pos db 0

secondary_start:
  cli
  cld

  mov si, welcome_message
  call display_message

  mov ah, 0
  mov dl, 0
  int 0x13
  jc secondary_start

  mov ax, LOAD_SEGMENT
  mov es, ax
  xor bx, bx

  mov SECTOR_TOTAL, 17
  mov CYLINDER_INDEX, 0
  mov SECTOR_INDEX, 2
  mov HEAD_INDEX, 0
  mov DRIVE_INDEX, 0
  mov ah, 0x02
  int 0x13

  mov ax, LOAD_SEGMENT
  mov es, ax
  mov bx, 512*17

  mov SECTOR_TOTAL, 18
  mov CYLINDER_INDEX, 0
  mov SECTOR_INDEX, 1
  mov HEAD_INDEX, 1
  mov DRIVE_INDEX, 0
  mov ah, 0x02
  int 0x13

  call initialize_gdt

  mov ax, 0x10
  mov ds, ax
  mov es, ax

  mov eax, cr0
  or eax, 1
  mov cr0, eax

  mov esp, 0xf000

  xor edx, edx
  xor eax, eax
  mov es, eax
  mov edx, [es:LOAD_MEMORY + 0x18]

  jmp edx

  cli
  hlt

move_cursor:
  mov bh, 0
  mov ah, 2
  int 10h

  mov [cursor_x_pos], dl
  mov [cursor_y_pos], dh
  ret

display_message:
.loop:
  lodsb
  or al, al
  jz .done
  mov cx, 1
  call print_character
  jmp .loop
.done:
  ret

print_character:
  mov bh, 0
  mov ah, 0ah
  int 10h

  add [cursor_x_pos], cx
  mov dl, [cursor_x_pos]
  mov dh, [cursor_y_pos]
  call move_cursor

  ret

initialize_gdt:
  cli
  pusha
  lgdt [gdt_descriptor]
  sti
  popa
  ret

gdt_entries:
  dd 0
  dd 0

  dw 0FFFFh
  dw 0x00
  db 0x00
  db 10011010b
  db 11001111b
  db 0x00

  dw 0FFFFh
  dw 0x0000
  db 0x00
  db 10010010b
  db 11001111b
  db 0

gdt_descriptor:
  dw gdt_entries - gdt_entries - 1
  dd gdt_entries

times 510 - ($-$$) db 0

dw 0xAA55
