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

# Add filesystem and memory management modules
FILESYSTEM_DIR := filesystem
MEMORY_DIR := memory

.PHONY: all build_dir disk qemu-gdb clean bootable

include $(FILESYSTEM_DIR)/FileSystemBuild.mk
include $(MEMORY_DIR)/MemoryBuild.mk

all: build_dir disk bootable

build_dir:
	mkdir -p build/kern build/boot

$(BOOTLOADER):
	make -f bootloader/BootBuild.mk -C bootloader CROSS_COMPILE=$(CROSS_COMPILE)

$(KERNEL):
	make -f kernel/KernelBuild.mk -C kernel CROSS_COMPILE=$(CROSS_COMPILE)

# Create a floppy disk image for QEMU testing
disk: $(BOOTLOADER) $(KERNEL)
	dd if=/dev/zero of=$(DISK_IMG) bs=512 count=2880
	dd if=$(BOOTLOADER) of=$(DISK_IMG) bs=512 count=1 seek=0
	dd if=$(KERNEL) of=$(DISK_IMG) bs=512 count=30 seek=1

# Create a bootable hard disk image for real hardware
bootable: $(BOOTLOADER) $(KERNEL)
	# Create a 32MB disk image (65536 sectors * 512 bytes)
	dd if=/dev/zero of=bootable.img bs=512 count=65536
	
	# Create partitioning tools script
	echo "o\nn\np\n1\n2048\n\na\nw\n" > fdisk.script
	
	# Create partition table
	fdisk bootable.img < fdisk.script
	rm fdisk.script
	
	# Write the bootloader to the MBR
	dd if=$(BOOTLOADER) of=bootable.img bs=446 count=1 conv=notrunc
	
	# Create a FAT filesystem in the partition
	LOOP_DEVICE=$$(sudo losetup --show -f -P bootable.img); \
	sudo mkfs.vfat -F 32 $${LOOP_DEVICE}p1; \
	mkdir -p mnt; \
	sudo mount $${LOOP_DEVICE}p1 mnt; \
	sudo cp $(KERNEL) mnt/kernel.bin; \
	# Create a simple GRUB configuration if needed
	if [ ! -d "mnt/boot/grub" ]; then \
		sudo mkdir -p mnt/boot/grub; \
		echo 'menuentry "uintOS" {' > grub.cfg; \
		echo '  multiboot /kernel.bin' >> grub.cfg; \
		echo '}' >> grub.cfg; \
		sudo cp grub.cfg mnt/boot/grub/; \
		rm grub.cfg; \
	fi; \
	sudo umount mnt; \
	sudo losetup -d $${LOOP_DEVICE}; \
	rmdir mnt
	
	@echo "Bootable disk image 'bootable.img' created successfully."
	@echo "You can write this image to a USB drive using 'dd'"
	@echo "Example: dd if=bootable.img of=/dev/sdX bs=4M status=progress && sync"

qemu-gdb:
	qemu-system-i386 $(QEMU_DEBUG) $(QEMU_STDIO) -machine q35 -fda $(DISK_IMG) -gdb tcp::26000 -D qemu.log -S

# Test with bootable hard disk image
qemu-bootable:
	qemu-system-i386 $(QEMU_STDIO) -machine q35 -hda bootable.img -m 128M

clean:
	killall qemu-system-i386 || true
	killall gdb || true
	rm -rf build $(DISK_IMG) bootable.img mnt
