# Build rules for the memory management module
MEMORY_OBJECTS := $(patsubst %.c, $(BUILD_DIR)/memory/%.o, $(wildcard *.c))

$(BUILD_DIR)/memory/%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

MEMORY_LIB := $(BUILD_DIR)/memory/libmemory.a

$(MEMORY_LIB): $(MEMORY_OBJECTS)
	$(AR) rcs $@ $^

.PHONY: clean
clean:
	rm -f $(MEMORY_OBJECTS) $(MEMORY_LIB)