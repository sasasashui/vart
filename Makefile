CC ?= cc
CFLAGS ?= -O2 -g
CPPFLAGS += -Iinclude
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror

BUILD_DIR := build
TARGET := $(BUILD_DIR)/vart
CORE_SOURCES := src/kvm.c src/memory.c src/vm.c
CORE_OBJECTS := $(CORE_SOURCES:src/%.c=$(BUILD_DIR)/%.o)
VART_OBJECTS := $(BUILD_DIR)/main.o $(CORE_OBJECTS)

TEST_TARGETS := $(BUILD_DIR)/tests/memory-region \
	$(BUILD_DIR)/tests/kvm-vm-memory

.PHONY: all clean check

all: $(TARGET)

$(TARGET): $(VART_OBJECTS)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/tests/kvm-vm-memory: tests/integration/kvm/vm-memory.c \
		$(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/tests/memory-region: tests/unit/memory/region.c \
		$(BUILD_DIR)/memory.o
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

check: $(TARGET) $(TEST_TARGETS)
	$(TARGET) --probe
	$(BUILD_DIR)/tests/memory-region
	$(BUILD_DIR)/tests/kvm-vm-memory

clean:
	rm -rf $(BUILD_DIR)
