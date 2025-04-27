OUTPUT_DIR=../build/boot
BOOTLOADER_BINARY=$(OUTPUT_DIR)/uintOS_bootloader

ASM_SOURCES := $(wildcard *.asm)
ASM_OBJECTS := $(patsubst %.asm, $(OUTPUT_DIR)/%.o, $(ASM_SOURCES))

all: $(BOOTLOADER_BINARY)

$(OUTPUT_DIR)/%.o: %.asm
	nasm -f elf -F dwarf -g $< -o $@

$(BOOTLOADER_BINARY): $(ASM_OBJECTS)
	ld -m elf_i386 -T bootloader.lds $< -o $@.bin
	objcopy -O binary $@.bin $@
