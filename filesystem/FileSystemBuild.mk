# Build rules for the filesystem module
FILESYSTEM_OBJECTS := $(patsubst %.c, $(BUILD_DIR)/filesystem/%.o, $(wildcard *.c))

# Add ext2 filesystem objects
EXT2_OBJECTS := $(patsubst %.c, $(BUILD_DIR)/filesystem/ext2/%.o, $(wildcard ext2/*.c))

# Add iso9660 filesystem objects
ISO9660_OBJECTS := $(patsubst %.c, $(BUILD_DIR)/filesystem/iso9660/%.o, $(wildcard iso9660/*.c))

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