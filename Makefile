CC ?= cc
CFLAGS ?= -O2 -g
CPPFLAGS += -Iinclude
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror

BUILD_DIR := build
TARGET := $(BUILD_DIR)/rvmm
SOURCES := src/main.c
OBJECTS := $(SOURCES:src/%.c=$(BUILD_DIR)/%.o)

.PHONY: all clean check

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

check: $(TARGET)
	$(TARGET) --probe

clean:
	rm -rf $(BUILD_DIR)

