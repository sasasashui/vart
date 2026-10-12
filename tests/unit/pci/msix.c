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
    BarState bar_state = { 0 };
    uint64_t value;
    uint8_t capability_offset;
    const uint8_t devfn = 8;

    vart_address_space_init(&address_space);
    vart_bus_init(&system_bus, "system-bus", NULL, &address_space);
    if (vart_pci_root_complex_init(&complex, &system_bus, &address_space) < 0 ||
        vart_bus_add_device(&system_bus, &complex.parent) < 0 ||
        vart_device_realize(&complex.parent) < 0 ||
        vart_pci_device_init(&device, "pci-msix-test", UINT16_C(0x1234),
                             UINT16_C(0x5678), 1, 0, 0, 2) < 0 ||
        vart_pci_device_set_bar(&device, 0, UINT64_C(0x2000), 0, &bar_ops,
                                &bar_state) < 0 ||
        vart_pci_device_init_msix(&device, 4, 0, 0x100, 0, 0x1000) < 0 ||
        vart_pci_bus_add_device(&complex.root_bus, &device, devfn) < 0 ||
        vart_device_realize(&device.parent) < 0) {
        fprintf(stderr, "not ok - MSI-X setup\n");
        return EXIT_FAILURE;
    }
    if (vart_address_space_write(&address_space, ecam_address(devfn, 0x04),
                                 2, VART_PCI_COMMAND_MEMORY_ENABLE) < 0) {
        fprintf(stderr, "not ok - MSI-X memory enablement\n");
        return EXIT_FAILURE;
    }
    capability_offset = device.msix.capability_offset;
    if (capability_offset != 0x40 ||
        vart_address_space_read(&address_space,
                                ecam_address(devfn, capability_offset), 4,
                                &value) < 0 ||
        value != UINT32_C(0x00030011) ||
        vart_address_space_read(&address_space,
                                ecam_address(devfn, capability_offset + 4), 4,
                                &value) < 0 || value != UINT32_C(0x00000100) ||
        vart_address_space_read(&address_space,
                                ecam_address(devfn, capability_offset + 8), 4,
                                &value) < 0 || value != UINT32_C(0x00001000) ||
        vart_address_space_write(&address_space,
                                 ecam_address(devfn, capability_offset + 4),
                                 4, UINT32_C(0xffffffff)) < 0 ||
        vart_address_space_read(&address_space,
                                ecam_address(devfn, capability_offset + 4), 4,
                                &value) < 0 || value != UINT32_C(0x00000100)) {
        fprintf(stderr, "not ok - MSI-X capability\n");
        return EXIT_FAILURE;
    }

    if (vart_address_space_write(&address_space,
                                 VART_VIRT_PCIE_MMIO_BASE + 0x100, 8,
                                 UINT64_C(0x1122334455667788)) < 0 ||
        vart_address_space_read(&address_space,
                                VART_VIRT_PCIE_MMIO_BASE + 0x100, 8,
                                &value) < 0 ||
        value != UINT64_C(0x1122334455667788) ||
        vart_address_space_write(&address_space,
                                 VART_VIRT_PCIE_MMIO_BASE + 0x1000, 8,
                                 UINT64_C(0xaabbccddeeff0011)) < 0 ||
        vart_address_space_read(&address_space,
                                VART_VIRT_PCIE_MMIO_BASE + 0x1000, 8,
                                &value) < 0 || value != 0) {
        fprintf(stderr, "not ok - MSI-X table and PBA access\n");
        return EXIT_FAILURE;
    }
    if (vart_address_space_write(
            &address_space, ecam_address(devfn, capability_offset + 2), 2,
            UINT16_C(0xc003)) < 0 || !device.msix.enabled ||
        !device.msix.function_mask) {
        fprintf(stderr, "not ok - MSI-X control\n");
        return EXIT_FAILURE;
    }
    if (vart_address_space_write(&address_space, ecam_address(devfn, 0x10),
                                 4, VART_VIRT_PCIE_MMIO_BASE + 0x10000) < 0 ||
        vart_address_space_read(&address_space,
                                VART_VIRT_PCIE_MMIO_BASE + 0x100, 8,
                                &value) != -ENOENT ||
        vart_address_space_write(&address_space,
                                 VART_VIRT_PCIE_MMIO_BASE + 0x10100, 8,
                                 UINT64_C(0x8877665544332211)) < 0 ||
        vart_address_space_read(&address_space,
                                VART_VIRT_PCIE_MMIO_BASE + 0x10100, 8,
                                &value) < 0 ||
        value != UINT64_C(0x8877665544332211)) {
        fprintf(stderr, "not ok - MSI-X BAR relocation\n");
        return EXIT_FAILURE;
    }

    vart_device_unrealize(&device.parent);
    if (vart_pci_bus_remove_device(&complex.root_bus, &device) < 0 ||
        vart_pci_root_complex_destroy(&complex) < 0 ||
        vart_bus_destroy(&system_bus) < 0) {
        fprintf(stderr, "not ok - MSI-X teardown\n");
        return EXIT_FAILURE;
    }
    vart_address_space_destroy(&address_space);
    puts("ok - MSI-X capability, table, and PBA");
    return EXIT_SUCCESS;
}
