BUILD_OUTPUT=../build
KERNEL_BINARY=$(BUILD_OUTPUT)/kern/kernel

COMPILER_FLAGS=-Wall -Wno-format
COMPILER_FLAGS+=-fno-stack-protector -fno-omit-frame-pointer -fno-asynchronous-unwind-tables
COMPILER_FLAGS+=-fno-builtin -masm=intel -m32 -nostdlib -gdwarf-2 -ggdb3 -save-temps

SOURCE_FILES := gdt.c io.c irq.c task.c lapic.c task1.c kernel.c
OBJECT_FILES := $(patsubst %.c, $(BUILD_OUTPUT)/kern/%.o, $(SOURCE_FILES))

# Add filesystem and memory management to kernel build
FILESYSTEM_SRC := $(FILESYSTEM_DIR)/fat12.c
MEMORY_SRC := $(MEMORY_DIR)/paging.c

KERNEL_SRC += $(FILESYSTEM_SRC) $(MEMORY_SRC)

all: $(KERNEL_BINARY)
	@echo "Compiling kernel source files: $(SOURCE_FILES)"
	@echo "Generated object files: $(OBJECT_FILES)"

$(BUILD_OUTPUT)/kern/%.o: %.c
	gcc $(COMPILER_FLAGS) -m32 -c $< -o $@

$(KERNEL_BINARY): $(OBJECT_FILES)
	ld --trace -m elf_i386 -Tkernel.lds $(OBJECT_FILES) -o $@
	# objcopy --only-keep-debug $(KERNEL_BINARY) $(KERNEL_BINARY).dbg
	# objcopy --strip-debug $(KERNEL_BINARY)
	# objcopy --add-gnu-debuglink=$(KERNEL_BINARY).dbg $(KERNEL_BINARY)

# Add a clean target to remove build artifacts
clean:
	rm -f *.o kernel
