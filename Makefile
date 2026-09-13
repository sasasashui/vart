CC ?= cc
OBJCOPY ?= objcopy
CFLAGS ?= -O2 -g
CPPFLAGS += -Iinclude
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror

BUILD_DIR := build
TARGET := $(BUILD_DIR)/vart
CORE_SOURCES := src/address-space.c src/exec.c src/kvm.c src/memory.c \
	src/vcpu.c src/vm.c src/devices/test-device.c src/devices/uart16550.c
CORE_OBJECTS := $(CORE_SOURCES:src/%.c=$(BUILD_DIR)/%.o)
VART_OBJECTS := $(BUILD_DIR)/main.o $(CORE_OBJECTS)

TEST_TARGETS := $(BUILD_DIR)/tests/memory-region \
	$(BUILD_DIR)/tests/address-region \
	$(BUILD_DIR)/tests/address-space-topology \
	$(BUILD_DIR)/tests/address-space-mmio \
	$(BUILD_DIR)/tests/exec-mmio-write \
	$(BUILD_DIR)/tests/test-device \
	$(BUILD_DIR)/tests/uart16550 \
	$(BUILD_DIR)/tests/virt-map \
	$(BUILD_DIR)/tests/kvm-vm-memory \
	$(BUILD_DIR)/tests/kvm-vcpu \
	$(BUILD_DIR)/tests/kvm-guest-mmio \
	$(BUILD_DIR)/tests/kvm-guest-mmio-roundtrip
GUEST_TARGETS := $(BUILD_DIR)/guests/cpu/mmio-exit.bin
GUEST_TARGETS += $(BUILD_DIR)/guests/cpu/mmio-roundtrip.bin

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

$(BUILD_DIR)/tests/address-region: tests/unit/address-space/region.c \
		$(BUILD_DIR)/address-space.o
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/tests/address-space-topology: \
		tests/unit/address-space/topology.c $(BUILD_DIR)/address-space.o
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/tests/address-space-mmio: \
		tests/unit/address-space/mmio.c $(BUILD_DIR)/address-space.o
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/tests/exec-mmio-write: tests/unit/exec/mmio-write.c \
		$(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/tests/test-device: tests/unit/devices/test-device.c \
		$(BUILD_DIR)/address-space.o $(BUILD_DIR)/devices/test-device.o
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/tests/uart16550: tests/unit/devices/uart16550.c \
		$(BUILD_DIR)/address-space.o $(BUILD_DIR)/devices/uart16550.o
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/tests/virt-map: tests/unit/machine/virt-map.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/tests/kvm-vcpu: tests/integration/kvm/vcpu.c $(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/tests/kvm-guest-mmio: tests/integration/kvm/guest-mmio.c \
		$(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/tests/kvm-guest-mmio-roundtrip: \
		tests/integration/kvm/guest-mmio-roundtrip.c $(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

$(BUILD_DIR)/guests/cpu/mmio-exit.elf: tests/guests/cpu/mmio-exit.S \
		tests/guests/cpu/linker.ld
	@mkdir -p $(dir $@)
	$(CC) -march=rv64i -mabi=lp64 -nostdlib -nostartfiles -static \
		-Wl,--build-id=none -Wl,-T,tests/guests/cpu/linker.ld \
		$< -o $@

$(BUILD_DIR)/guests/cpu/mmio-exit.bin: \
		$(BUILD_DIR)/guests/cpu/mmio-exit.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/guests/cpu/mmio-roundtrip.elf: \
		tests/guests/cpu/mmio-roundtrip.S tests/guests/cpu/linker.ld
	@mkdir -p $(dir $@)
	$(CC) -march=rv64i -mabi=lp64 -nostdlib -nostartfiles -static \
		-Wl,--build-id=none -Wl,-T,tests/guests/cpu/linker.ld $< -o $@

$(BUILD_DIR)/guests/cpu/mmio-roundtrip.bin: \
		$(BUILD_DIR)/guests/cpu/mmio-roundtrip.elf
	$(OBJCOPY) -O binary $< $@

check: $(TARGET) $(TEST_TARGETS) $(GUEST_TARGETS)
	$(TARGET) --probe
	$(BUILD_DIR)/tests/address-region
	$(BUILD_DIR)/tests/address-space-topology
	$(BUILD_DIR)/tests/address-space-mmio
	$(BUILD_DIR)/tests/exec-mmio-write
	$(BUILD_DIR)/tests/test-device
	$(BUILD_DIR)/tests/uart16550
	$(BUILD_DIR)/tests/virt-map
	$(BUILD_DIR)/tests/memory-region
	$(BUILD_DIR)/tests/kvm-vm-memory
	$(BUILD_DIR)/tests/kvm-vcpu
	$(BUILD_DIR)/tests/kvm-guest-mmio \
		$(BUILD_DIR)/guests/cpu/mmio-exit.bin
	$(BUILD_DIR)/tests/kvm-guest-mmio-roundtrip \
		$(BUILD_DIR)/guests/cpu/mmio-roundtrip.bin

clean:
	rm -rf $(BUILD_DIR)
