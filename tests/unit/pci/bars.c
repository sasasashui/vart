#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "vart/machine/virt.h"
#include "vart/pci.h"

typedef struct BarState {
    uint64_t value;
} BarState;

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
    BarState bar0_state = { .value = 0 };
    BarState bar2_state = { .value = 0 };
    uint64_t bar0_address;
    uint64_t bar2_address;
    uint64_t value;
    const uint8_t devfn = 8;

    vart_address_space_init(&address_space);
    vart_bus_init(&system_bus, "system-bus", NULL, &address_space);
    if (vart_pci_root_complex_init(&complex, &system_bus, &address_space) < 0 ||
        vart_bus_add_device(&system_bus, &complex.parent) < 0 ||
        vart_device_realize(&complex.parent) < 0 ||
        vart_pci_device_init(&device, "pci-bar-test", UINT16_C(0x1234),
                             UINT16_C(0x5678), 1, 0, 0, 2) < 0 ||
        vart_pci_device_set_bar(&device, 0, UINT64_C(0x1000), 0,
                                &bar_ops, &bar0_state) < 0 ||
        vart_pci_device_set_bar(&device, 2, UINT64_C(0x2000),
                                VART_PCI_BAR_MEM_64, &bar_ops,
                                &bar2_state) < 0 ||
        vart_pci_bus_add_device(&complex.root_bus, &device, devfn) < 0 ||
        vart_device_realize(&device.parent) < 0) {
        fprintf(stderr, "not ok - PCI BAR setup\n");
        return EXIT_FAILURE;
    }

    bar0_address = VART_VIRT_PCIE_MMIO_BASE;
    bar2_address = VART_VIRT_PCIE_MMIO_BASE + UINT64_C(0x2000);
    if (vart_address_space_read(&address_space,
                                ecam_address(devfn, 0x10), 4, &value) < 0 ||
        value != bar0_address ||
        vart_address_space_read(&address_space,
                                ecam_address(devfn, 0x18), 4, &value) < 0 ||
        value != ((uint32_t)bar2_address | VART_PCI_BAR_MEM_64) ||
        vart_address_space_read(&address_space,
                                ecam_address(devfn, 0x1c), 4, &value) < 0 ||
        value != bar2_address >> 32) {
        fprintf(stderr, "not ok - PCI BAR assignment\n");
        return EXIT_FAILURE;
    }

    if (vart_address_space_write(&address_space,
                                 ecam_address(devfn, 0x04), 2,
                                 VART_PCI_COMMAND_MEMORY_ENABLE) < 0 ||
        vart_address_space_write(&address_space, bar0_address, 8,
                                 UINT64_C(0x1122334455667788)) < 0 ||
        bar0_state.value != UINT64_C(0x1122334455667788) ||
        vart_address_space_read(&address_space, bar2_address, 8, &value) < 0 ||
        value != bar2_state.value) {
        fprintf(stderr, "not ok - PCI BAR MMIO access\n");
        return EXIT_FAILURE;
    }

    if (vart_address_space_write(&address_space, ecam_address(devfn, 0x04),
                                 2, 0) < 0 ||
        vart_address_space_read(&address_space, bar0_address, 8, &value) !=
            -ENOENT ||
        vart_address_space_write(&address_space, ecam_address(devfn, 0x04),
                                 2, VART_PCI_COMMAND_MEMORY_ENABLE) < 0) {
        fprintf(stderr, "not ok - PCI BAR memory enablement\n");
        return EXIT_FAILURE;
    }

    if (vart_pci_device_reset(&device) < 0 || device.memory_enabled ||
        device.bus_master_enabled || device.bars[0].mapped ||
        vart_address_space_read(&address_space, ecam_address(devfn, 0x04), 2,
                                &value) < 0 || value != 0 ||
        vart_address_space_read(&address_space, bar0_address, 8, &value) !=
            -ENOENT ||
        vart_address_space_write(&address_space, ecam_address(devfn, 0x04),
                                 2, VART_PCI_COMMAND_MEMORY_ENABLE) < 0) {
        fprintf(stderr, "not ok - PCI BAR reset lifecycle\n");
        return EXIT_FAILURE;
    }

    if (vart_address_space_write(&address_space, ecam_address(devfn, 0x10),
                                 4, UINT32_MAX) < 0 ||
        vart_address_space_read(&address_space, ecam_address(devfn, 0x10),
                                4, &value) < 0 ||
        value != UINT32_C(0xfffff000) ||
        vart_address_space_write(&address_space, ecam_address(devfn, 0x10),
                                 4, VART_VIRT_PCIE_MMIO_BASE + 0x10000) < 0 ||
        vart_address_space_read(&address_space, ecam_address(devfn, 0x10),
                                4, &value) < 0 ||
        value != VART_VIRT_PCIE_MMIO_BASE + 0x10000 ||
        vart_address_space_read(&address_space, bar0_address, 8, &value) !=
            -ENOENT ||
        vart_address_space_write(&address_space,
                                 VART_VIRT_PCIE_MMIO_BASE + 0x10000, 8,
                                 UINT64_C(0xaabbccddeeff0011)) < 0 ||
        bar0_state.value != UINT64_C(0xaabbccddeeff0011)) {
        fprintf(stderr, "not ok - PCI BAR probe and relocation\n");
        return EXIT_FAILURE;
    }

    if (vart_pci_bus_remove_device(&complex.root_bus, &device) != -EBUSY) {
        fprintf(stderr, "not ok - PCI BAR realized removal\n");
        return EXIT_FAILURE;
    }
    vart_device_unrealize(&device.parent);
    if (vart_pci_bus_remove_device(&complex.root_bus, &device) < 0 ||
        vart_pci_root_complex_destroy(&complex) < 0 ||
        vart_bus_destroy(&system_bus) < 0) {
        fprintf(stderr, "not ok - PCI BAR teardown\n");
        return EXIT_FAILURE;
    }
    vart_address_space_destroy(&address_space);
    puts("ok - PCI BAR assignment, MMIO, probe, and relocation");
    return EXIT_SUCCESS;
}
