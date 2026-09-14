CC ?= cc
OBJCOPY ?= objcopy
CFLAGS ?= -O2 -g
CPPFLAGS += -Iinclude
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Werror -pthread -MMD -MP

CONFIG_DEBUG_LOCKS ?= n
LINUX_6_18_IMAGE ?= ../linux-build-6.18.3/arch/riscv/boot/Image
LINUX_7_3_RC2_IMAGE ?= ../linux-build-7.3-rc2/arch/riscv/boot/Image
LINUX_INITRD ?= ../rootfs.cpio.gz
ifneq ($(filter y 1,$(CONFIG_DEBUG_LOCKS)),)
CPPFLAGS += -DCONFIG_DEBUG_LOCKS
endif

BUILD_DIR := build
TARGET := $(BUILD_DIR)/vart
CORE_SOURCES := src/address-space.c src/cli.c src/event-loop.c src/exec.c \
	src/fdt.c src/irq.c \
	src/kvm.c \
	src/kvm-device.c src/memory.c src/riscv-aia.c src/riscv-cpu.c \
	src/riscv-kvm.c src/riscv-sbi.c \
	src/sync.c src/vcpu.c src/vm.c src/machine/virt-fdt.c \
	src/machine/virt-loader.c src/machine/virt-machine.c \
	src/machine/virt-machine-loader.c \
	src/devices/test-device.c \
	src/devices/uart16550.c
CORE_OBJECTS := $(CORE_SOURCES:src/%.c=$(BUILD_DIR)/%.o)
VART_OBJECTS := $(BUILD_DIR)/main.o $(CORE_OBJECTS)

TEST_TARGETS := $(BUILD_DIR)/tests/memory-region \
	$(BUILD_DIR)/tests/cli-options \
	$(BUILD_DIR)/tests/irq-line \
	$(BUILD_DIR)/tests/event-loop \
	$(BUILD_DIR)/tests/sync-lock \
	$(BUILD_DIR)/tests/address-region \
	$(BUILD_DIR)/tests/address-space-topology \
	$(BUILD_DIR)/tests/address-space-mmio \
	$(BUILD_DIR)/tests/exec-mmio-write \
	$(BUILD_DIR)/tests/test-device \
	$(BUILD_DIR)/tests/uart16550 \
	$(BUILD_DIR)/tests/virt-map \
	$(BUILD_DIR)/tests/virt-loader \
	$(BUILD_DIR)/tests/riscv-boot-contract \
	$(BUILD_DIR)/tests/fdt-builder \
	$(BUILD_DIR)/tests/fdt-virt-machine \
	$(BUILD_DIR)/tests/kvm-vm-memory \
	$(BUILD_DIR)/tests/kvm-vcpu \
	$(BUILD_DIR)/tests/kvm-riscv-capabilities \
	$(BUILD_DIR)/tests/kvm-riscv-aia \
	$(BUILD_DIR)/tests/kvm-imsic-single \
	$(BUILD_DIR)/tests/kvm-imsic-smp \
	$(BUILD_DIR)/tests/kvm-aplic-single \
	$(BUILD_DIR)/tests/kvm-aplic-imsic-smp \
	$(BUILD_DIR)/tests/kvm-device-aplic \
	$(BUILD_DIR)/tests/kvm-uart-rx \
	$(BUILD_DIR)/tests/kvm-virt-machine \
	$(BUILD_DIR)/tests/kvm-virt-machine-loader \
	$(BUILD_DIR)/tests/kvm-riscv-registers \
	$(BUILD_DIR)/tests/kvm-riscv-direct-boot \
	$(BUILD_DIR)/tests/kvm-virt-fdt-boot \
	$(BUILD_DIR)/tests/kvm-sbi-base \
	$(BUILD_DIR)/tests/kvm-sbi-services \
	$(BUILD_DIR)/tests/kvm-sbi-hsm \
	$(BUILD_DIR)/tests/kvm-sbi-srst \
	$(BUILD_DIR)/tests/kvm-sbi-userspace \
	$(BUILD_DIR)/tests/kvm-vcpu-kick \
	$(BUILD_DIR)/tests/kvm-smp-shared-atomic \
	$(BUILD_DIR)/tests/kvm-smp-concurrent-mmio \
	$(BUILD_DIR)/tests/kvm-smp-hart-state \
	$(BUILD_DIR)/tests/kvm-guest-mmio \
	$(BUILD_DIR)/tests/kvm-guest-mmio-roundtrip
GUEST_TARGETS := $(BUILD_DIR)/guests/cpu/mmio-exit.bin
GUEST_TARGETS += $(BUILD_DIR)/guests/cpu/mmio-roundtrip.bin
GUEST_TARGETS += $(BUILD_DIR)/guests/cpu/spin.bin
GUEST_TARGETS += $(BUILD_DIR)/guests/cpu/direct-boot.bin
GUEST_TARGETS += $(BUILD_DIR)/guests/fdt/header.bin
GUEST_TARGETS += $(BUILD_DIR)/guests/aia/imsic-single.bin
GUEST_TARGETS += $(BUILD_DIR)/guests/aia/imsic-smp.bin
GUEST_TARGETS += $(BUILD_DIR)/guests/aia/aplic-single.bin
GUEST_TARGETS += $(BUILD_DIR)/guests/aia/aplic-imsic-smp.bin
GUEST_TARGETS += $(BUILD_DIR)/guests/aia/device-aplic.bin
GUEST_TARGETS += $(BUILD_DIR)/guests/aia/uart-rx.bin
GUEST_TARGETS += $(BUILD_DIR)/guests/sbi/base.bin
GUEST_TARGETS += $(BUILD_DIR)/guests/sbi/services.bin
GUEST_TARGETS += $(BUILD_DIR)/guests/sbi/hsm.bin
GUEST_TARGETS += $(BUILD_DIR)/guests/sbi/srst-shutdown.bin
GUEST_TARGETS += $(BUILD_DIR)/guests/sbi/srst-cold-reboot.bin
GUEST_TARGETS += $(BUILD_DIR)/guests/sbi/srst-warm-reboot.bin
GUEST_TARGETS += $(BUILD_DIR)/guests/sbi/userspace.bin
GUEST_TARGETS += $(BUILD_DIR)/guests/smp/shared-atomic.bin
GUEST_TARGETS += $(BUILD_DIR)/guests/smp/concurrent-mmio.bin
GUEST_TARGETS += $(BUILD_DIR)/guests/smp/hart-state.bin
DEPFILES := $(VART_OBJECTS:.o=.d) $(TEST_TARGETS:%=%.d)

.PHONY: all clean check check-debug-locks check-linux-6.18.3 \
	check-linux-6.18.3-devices check-linux-6.18.3-userspace \
	check-linux-6.18.3-repeat check-linux-7.3-rc2-smp

all: $(TARGET)

$(TARGET): $(VART_OBJECTS)
	$(CC) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/tests/kvm-vm-memory: tests/integration/kvm/vm-memory.c \
		$(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/memory-region: tests/unit/memory/region.c \
		$(BUILD_DIR)/memory.o
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/cli-options: tests/unit/cli/options.c \
		$(BUILD_DIR)/cli.o
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/irq-line: tests/unit/irq/line.c $(BUILD_DIR)/irq.o
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/event-loop: tests/unit/event/loop.c \
		$(BUILD_DIR)/event-loop.o
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/sync-lock: tests/unit/sync/lock.c $(BUILD_DIR)/sync.o
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/address-region: tests/unit/address-space/region.c \
		$(BUILD_DIR)/address-space.o
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/address-space-topology: \
		tests/unit/address-space/topology.c $(BUILD_DIR)/address-space.o
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/address-space-mmio: \
		tests/unit/address-space/mmio.c $(BUILD_DIR)/address-space.o
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/exec-mmio-write: tests/unit/exec/mmio-write.c \
		$(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/test-device: tests/unit/devices/test-device.c \
		$(BUILD_DIR)/address-space.o $(BUILD_DIR)/irq.o \
		$(BUILD_DIR)/devices/test-device.o
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/uart16550: tests/unit/devices/uart16550.c \
		$(BUILD_DIR)/address-space.o $(BUILD_DIR)/irq.o \
		$(BUILD_DIR)/devices/uart16550.o
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/virt-map: tests/unit/machine/virt-map.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/virt-loader: tests/unit/machine/virt-loader.c \
		$(BUILD_DIR)/memory.o $(BUILD_DIR)/fdt.o \
		$(BUILD_DIR)/machine/virt-fdt.o $(BUILD_DIR)/machine/virt-loader.o
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/riscv-boot-contract: \
		tests/unit/riscv/boot-contract.c $(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/fdt-builder: tests/unit/fdt/builder.c \
		$(BUILD_DIR)/fdt.o
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/fdt-virt-machine: tests/unit/fdt/virt-machine.c \
		$(BUILD_DIR)/fdt.o $(BUILD_DIR)/machine/virt-fdt.o
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/kvm-vcpu: tests/integration/kvm/vcpu.c $(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/kvm-riscv-capabilities: \
		tests/integration/kvm/riscv-capabilities.c $(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/kvm-riscv-aia: \
		tests/integration/kvm/riscv-aia.c $(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/kvm-imsic-single: \
		tests/integration/kvm/imsic-single.c $(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/kvm-imsic-smp: \
		tests/integration/kvm/imsic-smp.c $(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/kvm-aplic-single: \
		tests/integration/kvm/aplic-single.c $(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/kvm-aplic-imsic-smp: \
		tests/integration/kvm/aplic-imsic-smp.c $(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/kvm-device-aplic: \
		tests/integration/kvm/device-aplic.c $(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/kvm-uart-rx: \
		tests/integration/kvm/uart-rx.c $(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/kvm-virt-machine: \
		tests/integration/kvm/virt-machine.c $(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/kvm-virt-machine-loader: \
		tests/integration/kvm/virt-machine-loader.c $(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/kvm-riscv-registers: \
		tests/integration/kvm/riscv-registers.c $(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/kvm-riscv-direct-boot: \
		tests/integration/kvm/riscv-direct-boot.c $(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/kvm-virt-fdt-boot: \
		tests/integration/kvm/virt-fdt-boot.c $(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/kvm-sbi-base: tests/integration/kvm/sbi-base.c \
		$(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/kvm-sbi-services: tests/integration/kvm/sbi-services.c \
		$(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/kvm-sbi-hsm: tests/integration/kvm/sbi-hsm.c \
		$(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/kvm-sbi-srst: tests/integration/kvm/sbi-srst.c \
		$(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/kvm-sbi-userspace: \
		tests/integration/kvm/sbi-userspace.c $(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/kvm-vcpu-kick: tests/integration/kvm/vcpu-kick.c \
		$(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/kvm-smp-shared-atomic: \
		tests/integration/kvm/smp-shared-atomic.c $(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/kvm-smp-concurrent-mmio: \
		tests/integration/kvm/smp-concurrent-mmio.c $(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/kvm-smp-hart-state: \
		tests/integration/kvm/smp-hart-state.c $(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/kvm-guest-mmio: tests/integration/kvm/guest-mmio.c \
		$(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/tests/kvm-guest-mmio-roundtrip: \
		tests/integration/kvm/guest-mmio-roundtrip.c $(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

$(BUILD_DIR)/guests/cpu/mmio-exit.elf: tests/guests/cpu/mmio-exit.S \
		tests/guests/cpu/linker.ld
	@mkdir -p $(dir $@)
	$(CC) -march=rv64i -mabi=lp64 -fno-pic -no-pie \
		-nostdlib -nostartfiles -static \
		-Wl,--build-id=none -Wl,-T,tests/guests/cpu/linker.ld \
		$< -o $@

$(BUILD_DIR)/guests/cpu/mmio-exit.bin: \
		$(BUILD_DIR)/guests/cpu/mmio-exit.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/guests/cpu/mmio-roundtrip.elf: \
		tests/guests/cpu/mmio-roundtrip.S tests/guests/cpu/linker.ld
	@mkdir -p $(dir $@)
	$(CC) -march=rv64i -mabi=lp64 -fno-pic -no-pie \
		-nostdlib -nostartfiles -static \
		-Wl,--build-id=none -Wl,-T,tests/guests/cpu/linker.ld $< -o $@

$(BUILD_DIR)/guests/cpu/mmio-roundtrip.bin: \
		$(BUILD_DIR)/guests/cpu/mmio-roundtrip.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/guests/cpu/spin.elf: tests/guests/cpu/spin.S \
		tests/guests/cpu/linker.ld
	@mkdir -p $(dir $@)
	$(CC) -march=rv64i -mabi=lp64 -fno-pic -no-pie \
		-nostdlib -nostartfiles -static \
		-Wl,--build-id=none -Wl,-T,tests/guests/cpu/linker.ld $< -o $@

$(BUILD_DIR)/guests/cpu/spin.bin: $(BUILD_DIR)/guests/cpu/spin.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/guests/cpu/direct-boot.elf: \
		tests/guests/cpu/direct-boot.S \
		tests/fixtures/riscv-direct-boot.h tests/guests/cpu/linker.ld
	@mkdir -p $(dir $@)
	$(CC) -march=rv64i_zicsr -mabi=lp64 -fno-pic -no-pie \
		-nostdlib -nostartfiles -static \
		-Wl,--build-id=none -Wl,-T,tests/guests/cpu/linker.ld $< -o $@

$(BUILD_DIR)/guests/cpu/direct-boot.bin: \
		$(BUILD_DIR)/guests/cpu/direct-boot.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/guests/fdt/header.elf: tests/guests/fdt/header.S \
		tests/guests/fdt/linker.ld
	@mkdir -p $(dir $@)
	$(CC) -march=rv64i -mabi=lp64 -fno-pic -no-pie \
		-nostdlib -nostartfiles -static \
		-Wl,--build-id=none -Wl,-T,tests/guests/fdt/linker.ld $< -o $@

$(BUILD_DIR)/guests/fdt/header.bin: $(BUILD_DIR)/guests/fdt/header.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/guests/aia/imsic-single.elf: \
		tests/guests/aia/imsic-single.S tests/guests/aia/linker.ld
	@mkdir -p $(dir $@)
	$(CC) -march=rv64ima_ssaia -mabi=lp64 -fno-pic -no-pie \
		-nostdlib -nostartfiles -static \
		-Wl,--build-id=none -Wl,-T,tests/guests/aia/linker.ld $< -o $@

$(BUILD_DIR)/guests/aia/imsic-single.bin: \
		$(BUILD_DIR)/guests/aia/imsic-single.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/guests/aia/imsic-smp.elf: tests/guests/aia/imsic-smp.S \
		tests/fixtures/aia-imsic-smp.h tests/guests/aia/linker.ld
	@mkdir -p $(dir $@)
	$(CC) -march=rv64ima_ssaia -mabi=lp64 -fno-pic -no-pie \
		-nostdlib -nostartfiles -static \
		-Wl,--build-id=none -Wl,-T,tests/guests/aia/linker.ld $< -o $@

$(BUILD_DIR)/guests/aia/imsic-smp.bin: \
		$(BUILD_DIR)/guests/aia/imsic-smp.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/guests/aia/aplic-single.elf: \
		tests/guests/aia/aplic-single.S tests/guests/aia/linker.ld
	@mkdir -p $(dir $@)
	$(CC) -march=rv64ima_ssaia -mabi=lp64 -fno-pic -no-pie \
		-nostdlib -nostartfiles -static \
		-Wl,--build-id=none -Wl,-T,tests/guests/aia/linker.ld $< -o $@

$(BUILD_DIR)/guests/aia/aplic-single.bin: \
		$(BUILD_DIR)/guests/aia/aplic-single.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/guests/aia/aplic-imsic-smp.elf: \
		tests/guests/aia/aplic-imsic-smp.S \
		tests/fixtures/aia-aplic-smp.h tests/guests/aia/linker.ld
	@mkdir -p $(dir $@)
	$(CC) -march=rv64ima_ssaia -mabi=lp64 -fno-pic -no-pie \
		-nostdlib -nostartfiles -static \
		-Wl,--build-id=none -Wl,-T,tests/guests/aia/linker.ld $< -o $@

$(BUILD_DIR)/guests/aia/aplic-imsic-smp.bin: \
		$(BUILD_DIR)/guests/aia/aplic-imsic-smp.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/guests/aia/device-aplic.elf: \
		tests/guests/aia/device-aplic.S tests/guests/aia/linker.ld
	@mkdir -p $(dir $@)
	$(CC) -march=rv64ima_ssaia -mabi=lp64 -fno-pic -no-pie \
		-nostdlib -nostartfiles -static \
		-Wl,--build-id=none -Wl,-T,tests/guests/aia/linker.ld $< -o $@

$(BUILD_DIR)/guests/aia/device-aplic.bin: \
		$(BUILD_DIR)/guests/aia/device-aplic.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/guests/aia/uart-rx.elf: \
		tests/guests/aia/uart-rx.S tests/guests/aia/linker.ld
	@mkdir -p $(dir $@)
	$(CC) -march=rv64ima_ssaia -mabi=lp64 -fno-pic -no-pie \
		-nostdlib -nostartfiles -static \
		-Wl,--build-id=none -Wl,-T,tests/guests/aia/linker.ld $< -o $@

$(BUILD_DIR)/guests/aia/uart-rx.bin: \
		$(BUILD_DIR)/guests/aia/uart-rx.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/guests/sbi/base.elf: tests/guests/sbi/base.S \
		tests/fixtures/sbi-base.h tests/guests/cpu/linker.ld
	@mkdir -p $(dir $@)
	$(CC) -march=rv64i -mabi=lp64 -fno-pic -no-pie \
		-nostdlib -nostartfiles -static \
		-Wl,--build-id=none -Wl,-T,tests/guests/cpu/linker.ld $< -o $@

$(BUILD_DIR)/guests/sbi/base.bin: $(BUILD_DIR)/guests/sbi/base.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/guests/sbi/services.elf: tests/guests/sbi/services.S \
		tests/fixtures/sbi-services.h tests/guests/cpu/linker.ld
	@mkdir -p $(dir $@)
	$(CC) -march=rv64ima_zicsr_zicntr -mabi=lp64 -fno-pic -no-pie \
		-nostdlib -nostartfiles -static \
		-Wl,--build-id=none -Wl,-T,tests/guests/cpu/linker.ld $< -o $@

$(BUILD_DIR)/guests/sbi/services.bin: \
		$(BUILD_DIR)/guests/sbi/services.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/guests/sbi/hsm.elf: tests/guests/sbi/hsm.S \
		tests/fixtures/sbi-hsm.h tests/guests/cpu/linker.ld
	@mkdir -p $(dir $@)
	$(CC) -march=rv64ima -mabi=lp64 -fno-pic -no-pie \
		-nostdlib -nostartfiles -static \
		-Wl,--build-id=none -Wl,-T,tests/guests/cpu/linker.ld $< -o $@

$(BUILD_DIR)/guests/sbi/hsm.bin: $(BUILD_DIR)/guests/sbi/hsm.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/guests/sbi/srst-shutdown.elf: tests/guests/sbi/srst.S \
		tests/fixtures/sbi-srst.h tests/guests/cpu/linker.ld
	@mkdir -p $(dir $@)
	$(CC) -DVART_SBI_SRST_TYPE=0 -DVART_SBI_SRST_REASON=0 \
		-march=rv64i -mabi=lp64 -fno-pic -no-pie \
		-nostdlib -nostartfiles -static \
		-Wl,--build-id=none -Wl,-T,tests/guests/cpu/linker.ld $< -o $@

$(BUILD_DIR)/guests/sbi/srst-cold-reboot.elf: tests/guests/sbi/srst.S \
		tests/fixtures/sbi-srst.h tests/guests/cpu/linker.ld
	@mkdir -p $(dir $@)
	$(CC) -DVART_SBI_SRST_TYPE=1 -DVART_SBI_SRST_REASON=1 \
		-march=rv64i -mabi=lp64 -fno-pic -no-pie \
		-nostdlib -nostartfiles -static \
		-Wl,--build-id=none -Wl,-T,tests/guests/cpu/linker.ld $< -o $@

$(BUILD_DIR)/guests/sbi/srst-warm-reboot.elf: tests/guests/sbi/srst.S \
		tests/fixtures/sbi-srst.h tests/guests/cpu/linker.ld
	@mkdir -p $(dir $@)
	$(CC) -DVART_SBI_SRST_TYPE=2 -DVART_SBI_SRST_REASON=0 \
		-march=rv64i -mabi=lp64 -fno-pic -no-pie \
		-nostdlib -nostartfiles -static \
		-Wl,--build-id=none -Wl,-T,tests/guests/cpu/linker.ld $< -o $@

$(BUILD_DIR)/guests/sbi/srst-%.bin: $(BUILD_DIR)/guests/sbi/srst-%.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/guests/sbi/userspace.elf: tests/guests/sbi/userspace.S \
		tests/fixtures/sbi-userspace.h tests/guests/cpu/linker.ld
	@mkdir -p $(dir $@)
	$(CC) -march=rv64i -mabi=lp64 -fno-pic -no-pie \
		-nostdlib -nostartfiles -static \
		-Wl,--build-id=none -Wl,-T,tests/guests/cpu/linker.ld $< -o $@

$(BUILD_DIR)/guests/sbi/userspace.bin: \
		$(BUILD_DIR)/guests/sbi/userspace.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/guests/smp/shared-atomic.elf: \
		tests/guests/smp/shared-atomic.S tests/fixtures/smp-shared.h \
		tests/guests/cpu/linker.ld
	@mkdir -p $(dir $@)
	$(CC) -march=rv64ima -mabi=lp64 -fno-pic -no-pie \
		-nostdlib -nostartfiles -static \
		-Wl,--build-id=none -Wl,-T,tests/guests/cpu/linker.ld $< -o $@

$(BUILD_DIR)/guests/smp/shared-atomic.bin: \
		$(BUILD_DIR)/guests/smp/shared-atomic.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/guests/smp/concurrent-mmio.elf: \
		tests/guests/smp/concurrent-mmio.S tests/fixtures/smp-mmio.h \
		tests/guests/cpu/linker.ld
	@mkdir -p $(dir $@)
	$(CC) -march=rv64ima -mabi=lp64 -fno-pic -no-pie \
		-nostdlib -nostartfiles -static \
		-Wl,--build-id=none -Wl,-T,tests/guests/cpu/linker.ld $< -o $@

$(BUILD_DIR)/guests/smp/concurrent-mmio.bin: \
		$(BUILD_DIR)/guests/smp/concurrent-mmio.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD_DIR)/guests/smp/hart-state.elf: tests/guests/smp/hart-state.S \
		tests/fixtures/smp-hart-state.h tests/guests/cpu/linker.ld
	@mkdir -p $(dir $@)
	$(CC) -march=rv64ima -mabi=lp64 -fno-pic -no-pie \
		-nostdlib -nostartfiles -static \
		-Wl,--build-id=none -Wl,-T,tests/guests/cpu/linker.ld $< -o $@

$(BUILD_DIR)/guests/smp/hart-state.bin: \
		$(BUILD_DIR)/guests/smp/hart-state.elf
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
	$(BUILD_DIR)/tests/virt-loader
	$(BUILD_DIR)/tests/riscv-boot-contract
	$(BUILD_DIR)/tests/fdt-builder $(BUILD_DIR)/tests/fdt-basic.dtb
	$(BUILD_DIR)/tests/fdt-virt-machine \
		$(BUILD_DIR)/tests/fdt-virt-machine.dtb
	$(BUILD_DIR)/tests/memory-region
	$(BUILD_DIR)/tests/cli-options
	$(BUILD_DIR)/tests/irq-line
	$(BUILD_DIR)/tests/sync-lock
	$(BUILD_DIR)/tests/kvm-vm-memory
	$(BUILD_DIR)/tests/kvm-vcpu
	$(BUILD_DIR)/tests/kvm-riscv-capabilities
	$(BUILD_DIR)/tests/kvm-riscv-aia
	$(BUILD_DIR)/tests/kvm-imsic-single \
		$(BUILD_DIR)/guests/aia/imsic-single.bin
	$(BUILD_DIR)/tests/kvm-imsic-smp \
		$(BUILD_DIR)/guests/aia/imsic-smp.bin
	$(BUILD_DIR)/tests/kvm-aplic-single \
		$(BUILD_DIR)/guests/aia/aplic-single.bin
	$(BUILD_DIR)/tests/kvm-aplic-imsic-smp \
		$(BUILD_DIR)/guests/aia/aplic-imsic-smp.bin
	$(BUILD_DIR)/tests/kvm-device-aplic \
		$(BUILD_DIR)/guests/aia/device-aplic.bin
	$(BUILD_DIR)/tests/kvm-uart-rx \
		$(BUILD_DIR)/guests/aia/uart-rx.bin
	$(BUILD_DIR)/tests/kvm-virt-machine \
		$(BUILD_DIR)/guests/cpu/mmio-roundtrip.bin
	$(BUILD_DIR)/tests/kvm-virt-machine-loader \
		$(BUILD_DIR)/guests/cpu/mmio-roundtrip.bin
	$(BUILD_DIR)/tests/kvm-riscv-registers
	$(BUILD_DIR)/tests/kvm-riscv-direct-boot \
		$(BUILD_DIR)/guests/cpu/direct-boot.bin
	$(BUILD_DIR)/tests/kvm-virt-fdt-boot \
		$(BUILD_DIR)/guests/fdt/header.bin
	$(BUILD_DIR)/tests/kvm-sbi-base $(BUILD_DIR)/guests/sbi/base.bin
	$(BUILD_DIR)/tests/kvm-sbi-services \
		$(BUILD_DIR)/guests/sbi/services.bin
	$(BUILD_DIR)/tests/kvm-sbi-hsm $(BUILD_DIR)/guests/sbi/hsm.bin
	$(BUILD_DIR)/tests/kvm-sbi-srst \
		$(BUILD_DIR)/guests/sbi/srst-shutdown.bin 1 0
	$(BUILD_DIR)/tests/kvm-sbi-srst \
		$(BUILD_DIR)/guests/sbi/srst-cold-reboot.bin 2 1
	$(BUILD_DIR)/tests/kvm-sbi-srst \
		$(BUILD_DIR)/guests/sbi/srst-warm-reboot.bin 2 0
	$(BUILD_DIR)/tests/kvm-sbi-userspace \
		$(BUILD_DIR)/guests/sbi/userspace.bin
	$(BUILD_DIR)/tests/kvm-vcpu-kick $(BUILD_DIR)/guests/cpu/spin.bin
	$(BUILD_DIR)/tests/kvm-smp-shared-atomic \
		$(BUILD_DIR)/guests/smp/shared-atomic.bin
	$(BUILD_DIR)/tests/kvm-smp-concurrent-mmio \
		$(BUILD_DIR)/guests/smp/concurrent-mmio.bin
	$(BUILD_DIR)/tests/kvm-smp-hart-state \
		$(BUILD_DIR)/guests/smp/hart-state.bin
	$(BUILD_DIR)/tests/kvm-guest-mmio \
		$(BUILD_DIR)/guests/cpu/mmio-exit.bin
	$(BUILD_DIR)/tests/kvm-guest-mmio-roundtrip \
		$(BUILD_DIR)/guests/cpu/mmio-roundtrip.bin
	$(TARGET) --kernel $(BUILD_DIR)/guests/cpu/mmio-roundtrip.bin \
		--memory 64M --test-device
	$(TARGET) --kernel $(BUILD_DIR)/guests/sbi/srst-shutdown.bin \
		--memory 64M --cpus 2

check-debug-locks:
	$(MAKE) BUILD_DIR=build-debug CONFIG_DEBUG_LOCKS=y check

check-linux-6.18.3: $(TARGET)
	sh tests/integration/linux/early-boot.sh $(TARGET) \
		$(LINUX_6_18_IMAGE) $(LINUX_INITRD)

$(BUILD_DIR)/tests/kvm-linux-aia-uart: \
		tests/integration/linux/aia-uart.c $(CORE_OBJECTS)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(filter %.c %.o,$^) -o $@

check-linux-6.18.3-devices: $(BUILD_DIR)/tests/kvm-linux-aia-uart
	$(BUILD_DIR)/tests/kvm-linux-aia-uart \
		$(LINUX_6_18_IMAGE) $(LINUX_INITRD)

check-linux-6.18.3-userspace: check-linux-6.18.3-devices

check-linux-6.18.3-repeat: $(BUILD_DIR)/tests/kvm-linux-aia-uart
	$(BUILD_DIR)/tests/kvm-linux-aia-uart \
		$(LINUX_6_18_IMAGE) $(LINUX_INITRD) 5

check-linux-7.3-rc2-smp: $(BUILD_DIR)/tests/kvm-linux-aia-uart
	$(BUILD_DIR)/tests/kvm-linux-aia-uart \
		$(LINUX_7_3_RC2_IMAGE) $(LINUX_INITRD) 1 2 \
		"Linux version 7.3.0-rc2"

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPFILES)
