# Build rules for the filesystem module
FILESYSTEM_OBJECTS := $(patsubst %.c, $(BUILD_DIR)/filesystem/%.o, $(wildcard *.c))

# Add ext2 filesystem objects
EXT2_OBJECTS := $(patsubst %.c, $(BUILD_DIR)/filesystem/ext2/%.o, $(wildcard ext2/*.c))

# Add iso9660 filesystem objects
ISO9660_OBJECTS := $(patsubst %.c, $(BUILD_DIR)/filesystem/iso9660/%.o, $(wildcard iso9660/*.c))

# Source files
FILESYSTEM_SOURCES := fat12.c vfs/vfs.c fat12_vfs_adapter.c ext2/ext2.c iso9660/iso9660.c

# Object files
FILESYSTEM_OBJECTS := $(FILESYSTEM_SOURCES:.c=.o)

# Include directories
FILESYSTEM_INCLUDES := -I./ -I../kernel -I../memory

# Compiler flags specific to filesystem
FILESYSTEM_CFLAGS := $(CFLAGS) $(FILESYSTEM_INCLUDES)

# Rules for building
%.o: %.c
	$(CC) $(FILESYSTEM_CFLAGS) -c $< -o $@

$(BUILD_DIR)/filesystem/%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/filesystem/ext2/%.o: ext2/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/filesystem/iso9660/%.o: iso9660/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

FILESYSTEM_LIB := $(BUILD_DIR)/filesystem/libfilesystem.a

$(FILESYSTEM_LIB): $(FILESYSTEM_OBJECTS) $(EXT2_OBJECTS) $(ISO9660_OBJECTS)
	$(AR) rcs $@ $^

.PHONY: clean
clean:
	rm -f $(FILESYSTEM_OBJECTS) $(EXT2_OBJECTS) $(ISO9660_OBJECTS) $(FILESYSTEM_LIB)

clean-filesystem:
	rm -f $(FILESYSTEM_OBJECTS)