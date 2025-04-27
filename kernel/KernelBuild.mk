BUILD_OUTPUT=../build
KERNEL_BINARY=$(BUILD_OUTPUT)/kern/kernel

COMPILER_FLAGS=-Wall -Wno-format
COMPILER_FLAGS+=-fno-stack-protector -fno-omit-frame-pointer -fno-asynchronous-unwind-tables
COMPILER_FLAGS+=-fno-builtin -masm=intel -m32 -nostdlib -gdwarf-2 -ggdb3 -save-temps

SOURCE_FILES := gdt.c io.c irq.c task.c lapic.c task1.c keyboard.c shell.c kernel.c
OBJECT_FILES := $(patsubst %.c, $(BUILD_OUTPUT)/kern/%.o, $(SOURCE_FILES))

# Add filesystem and memory management to kernel build
FILESYSTEM_DIR ?= ../filesystem
MEMORY_DIR ?= ../memory

FILESYSTEM_SRC := $(FILESYSTEM_DIR)/fat12.c
MEMORY_SRC := $(MEMORY_DIR)/paging.c $(MEMORY_DIR)/heap.c

FILESYSTEM_OBJ := $(patsubst %.c, $(BUILD_OUTPUT)/%.o, $(FILESYSTEM_SRC))
MEMORY_OBJ := $(patsubst %.c, $(BUILD_OUTPUT)/%.o, $(MEMORY_SRC))

ALL_OBJECTS := $(OBJECT_FILES) $(FILESYSTEM_OBJ) $(MEMORY_OBJ)

all: $(KERNEL_BINARY)
	@echo "Compiling kernel source files: $(SOURCE_FILES)"
	@echo "Generated object files: $(ALL_OBJECTS)"

$(BUILD_OUTPUT)/kern/%.o: %.c
	gcc $(COMPILER_FLAGS) -m32 -c $< -o $@

$(BUILD_OUTPUT)/filesystem/%.o: $(FILESYSTEM_DIR)/%.c
	mkdir -p $(BUILD_OUTPUT)/filesystem
	gcc $(COMPILER_FLAGS) -m32 -c $< -o $@

$(BUILD_OUTPUT)/memory/%.o: $(MEMORY_DIR)/%.c
	mkdir -p $(BUILD_OUTPUT)/memory
	gcc $(COMPILER_FLAGS) -m32 -c $< -o $@

$(KERNEL_BINARY): $(ALL_OBJECTS)
	ld --trace -m elf_i386 -Tkernel.lds $(ALL_OBJECTS) -o $@
	# objcopy --only-keep-debug $(KERNEL_BINARY) $(KERNEL_BINARY).dbg
	# objcopy --strip-debug $(KERNEL_BINARY)
	# objcopy --add-gnu-debuglink=$(KERNEL_BINARY).dbg $(KERNEL_BINARY)

# Add a clean target to remove build artifacts
clean:
	rm -f *.o kernel $(BUILD_OUTPUT)/kern/*.o $(BUILD_OUTPUT)/filesystem/*.o $(BUILD_OUTPUT)/memory/*.o
