#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "vart/machine/virt.h"
#include "vart/pci.h"

typedef struct BarState {
    uint64_t value;
} BarState;

typedef struct Sink {
    uint32_t line;
    bool levels[8];
    unsigned int count;
} Sink;

typedef struct Injector {
    unsigned int count;
    unsigned int vector;
} Injector;

static int bar_read(void *opaque, uint64_t offset, unsigned int size,
                    uint64_t *value)
{
    BarState *state = opaque;

    (void)offset;
    if (size != 4 && size != 8) {
        return -EINVAL;
    }
    *value = state->value;
    return 0;
}

static int bar_write(void *opaque, uint64_t offset, unsigned int size,
                     uint64_t value)
{
    BarState *state = opaque;

    (void)offset;
    if (size != 4 && size != 8) {
        return -EINVAL;
    }
    state->value = value;
    return 0;
}

static int sink_set(void *opaque, uint32_t line, bool level)
{
    Sink *sink = opaque;

    if (sink->count >= sizeof(sink->levels) / sizeof(sink->levels[0])) {
        return -ENOSPC;
    }
    sink->line = line;
    sink->levels[sink->count++] = level;
    return 0;
}

static int inject_msix(VartPciDevice *device, unsigned int vector, void *opaque)
{
    Injector *injector = opaque;

    (void)device;
    injector->count++;
    injector->vector = vector;
    return 0;
}

static const VartMmioOps bar_ops = {
    .read = bar_read,
    .write = bar_write,
};

static uint64_t ecam_address(uint8_t devfn, uint16_t offset)
{
    return VART_VIRT_PCIE_ECAM_BASE + ((uint64_t)devfn << 12) + offset;
}

int main(void)
{
    VartAddressSpace address_space;
    VartBus system_bus;
    VartPciRootComplex complex;
    VartPciDevice device;
    BarState bar_state = { 0 };
    Sink sink = { 0 };
    Injector injector = { 0 };
    VartIrq irq;
    uint64_t value;
    uint8_t capability_offset;
    const uint8_t devfn = 8;

    if (vart_irq_init(&irq, sink_set, &sink, 23) < 0) {
        fprintf(stderr, "not ok - MSI-X IRQ setup\n");
        return EXIT_FAILURE;
    }
    vart_address_space_init(&address_space);
    vart_bus_init(&system_bus, "system-bus", NULL, &address_space);
    if (vart_pci_root_complex_init(&complex, &system_bus, &address_space) < 0 ||
        vart_bus_add_device(&system_bus, &complex.parent) < 0 ||
        vart_device_realize(&complex.parent) < 0 ||
        vart_pci_device_init(&device, "pci-msix-irq-test", UINT16_C(0x1234),
                             UINT16_C(0x5678), 1, 0, 0, 2) < 0 ||
        vart_pci_device_set_bar(&device, 0, UINT64_C(0x2000), 0, &bar_ops,
                                &bar_state) < 0 ||
        vart_pci_device_init_msix(&device, 4, 0, 0x100, 0, 0x1000) < 0 ||
        vart_pci_bus_add_device(&complex.root_bus, &device, devfn) < 0 ||
        vart_device_realize(&device.parent) < 0 ||
        vart_pci_device_connect_msix(&device, 0, &irq) < 0) {
        fprintf(stderr, "not ok - MSI-X IRQ device setup\n");
        return EXIT_FAILURE;
    }
    if (vart_address_space_write(&address_space, ecam_address(devfn, 0x04),
                                 2, VART_PCI_COMMAND_MEMORY_ENABLE) < 0) {
        fprintf(stderr, "not ok - MSI-X IRQ memory enablement\n");
        return EXIT_FAILURE;
    }
    capability_offset = device.msix.capability_offset;
    if (vart_address_space_write(&address_space,
                                 ecam_address(devfn, capability_offset + 2),
                                 2, UINT16_C(0x8003)) < 0 ||
        vart_pci_device_raise_msix(&device, 0) < 0 || sink.count != 2 ||
        !sink.levels[0] || sink.levels[1] || sink.line != 23 ||
        vart_address_space_read(&address_space,
                                VART_VIRT_PCIE_MMIO_BASE + 0x1000, 8,
                                &value) < 0 || value != 0) {
        fprintf(stderr, "not ok - MSI-X vector delivery\n");
        return EXIT_FAILURE;
    }
    if (vart_pci_device_set_msix_injector(&device, inject_msix, &injector) < 0 ||
        vart_pci_device_raise_msix(&device, 1) < 0 || injector.count != 1 ||
        injector.vector != 1) {
        fprintf(stderr, "not ok - MSI-X injector delivery\n");
        return EXIT_FAILURE;
    }
    if (vart_address_space_write(&address_space,
                                 VART_VIRT_PCIE_MMIO_BASE + 0x10c, 4, 1) < 0 ||
        vart_pci_device_raise_msix(&device, 0) < 0 || sink.count != 2 ||
        vart_address_space_read(&address_space,
                                VART_VIRT_PCIE_MMIO_BASE + 0x1000, 8,
                                &value) < 0 || value != 1 ||
        vart_address_space_write(&address_space,
                                 VART_VIRT_PCIE_MMIO_BASE + 0x10c, 4, 0) < 0 ||
        sink.count != 4 || !sink.levels[2] || sink.levels[3]) {
        fprintf(stderr, "not ok - MSI-X vector masking\n");
        return EXIT_FAILURE;
    }
    if (vart_address_space_write(&address_space,
                                 ecam_address(devfn, capability_offset + 2),
                                 2, UINT16_C(0xc003)) < 0 ||
        vart_pci_device_raise_msix(&device, 0) < 0 || sink.count != 4 ||
        vart_address_space_read(&address_space,
                                VART_VIRT_PCIE_MMIO_BASE + 0x1000, 8,
                                &value) < 0 || value != 1 ||
        vart_address_space_write(&address_space,
                                 ecam_address(devfn, capability_offset + 2),
                                 2, UINT16_C(0x8003)) < 0 ||
        sink.count != 6 || !sink.levels[4] || sink.levels[5]) {
        fprintf(stderr, "not ok - MSI-X function masking\n");
        return EXIT_FAILURE;
    }

    vart_device_unrealize(&device.parent);
    if (vart_pci_bus_remove_device(&complex.root_bus, &device) < 0 ||
        vart_pci_root_complex_destroy(&complex) < 0 ||
        vart_bus_destroy(&system_bus) < 0) {
        fprintf(stderr, "not ok - MSI-X IRQ teardown\n");
        return EXIT_FAILURE;
    }
    vart_address_space_destroy(&address_space);
    puts("ok - MSI-X vector delivery and masking");
    return EXIT_SUCCESS;
}
