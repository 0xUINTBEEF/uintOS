# Build rules for the filesystem module
FILESYSTEM_OBJECTS := $(patsubst %.c, $(BUILD_DIR)/filesystem/%.o, $(wildcard *.c))

$(BUILD_DIR)/filesystem/%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

FILESYSTEM_LIB := $(BUILD_DIR)/filesystem/libfilesystem.a

$(FILESYSTEM_LIB): $(FILESYSTEM_OBJECTS)
	$(AR) rcs $@ $^

.PHONY: clean
clean:
	rm -f $(FILESYSTEM_OBJECTS) $(FILESYSTEM_LIB)