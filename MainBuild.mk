BUILD_DIR=build
BOOTLOADER=$(BUILD_DIR)/boot/bootloader
KERNEL=$(BUILD_DIR)/kern/kernel
DISK_IMG=disk.img
QEMU_DEBUG=-d int,mmu,pcall,guest_errors,cpu_reset
QEMU_STDIO= -monitor stdio

# Add cross-compilation support
CROSS_COMPILE ?= i686-elf-
CC = $(CROSS_COMPILE)gcc
LD = $(CROSS_COMPILE)ld

all: build_dir disk

build_dir:
	[ -d build/ ] || mkdir build
	[ -d build/kern ] || mkdir build/kern
	[ -d build/boot ] || mkdir build/boot

.PHONY:
$(BOOTLOADER):
	make -f bootloader/BootBuild.mk -C bootloader CROSS_COMPILE=$(CROSS_COMPILE)

.PHONY:
$(KERNEL):
	make -f kernel/KernelBuild.mk -C kernel CROSS_COMPILE=$(CROSS_COMPILE)

.PHONY:
disk: $(BOOTLOADER) $(KERNEL)
	make -f bootloader/BootBuild.mk -C bootloader CROSS_COMPILE=$(CROSS_COMPILE)
	make -f kernel/KernelBuild.mk -C kernel CROSS_COMPILE=$(CROSS_COMPILE)
	dd if=/dev/zero of=$(DISK_IMG) bs=512 count=2880
	dd if=$(BOOTLOADER) of=$(DISK_IMG) bs=512 count=1 seek=0
	dd if=$(KERNEL) of=$(DISK_IMG) bs=512 count=30 seek=1

qemu-gdb:
	qemu-system-i386 $(QEMU_DEBUG) $(QEMU_STDIO) -machine q35 -fda $(DISK_IMG)  -gdb tcp::26000 -D qemu.log -S

.PHONY:
clean:
	killall qemu-system-i386 || true
	killall gdb || true
	rm -rf build $(DISK_IMG)
