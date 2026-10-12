#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "vart/machine/virt.h"
#include "vart/pci.h"

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
    uint8_t first_data[] = { 0x11, 0x22 };
    uint8_t second_data[] = { 0x33, 0x44, 0x55 };
    uint8_t first_offset;
    uint8_t second_offset;
    uint64_t value;
    const uint8_t devfn = 10;

    vart_address_space_init(&address_space);
    vart_bus_init(&system_bus, "system-bus", NULL, &address_space);
    if (vart_pci_root_complex_init(&complex, &system_bus, &address_space) < 0 ||
        vart_bus_add_device(&system_bus, &complex.parent) < 0 ||
        vart_device_realize(&complex.parent) < 0 ||
        vart_pci_device_init(&device, "pci-capability-test",
                             UINT16_C(0x1234), UINT16_C(0x5678), 1, 0, 0, 2) <
            0 ||
        vart_pci_device_add_capability(&device, 0x01, first_data,
                                       sizeof(first_data), &first_offset) < 0 ||
        first_offset != 0x40 ||
        vart_pci_device_add_capability(&device, 0x05, second_data,
                                       sizeof(second_data), &second_offset) < 0 ||
        second_offset != 0x44 ||
        vart_pci_bus_add_device(&complex.root_bus, &device, devfn) < 0 ||
        vart_device_realize(&device.parent) < 0 ||
        vart_address_space_read(&address_space,
                                ecam_address(devfn,
                                             VART_PCI_CAPABILITY_LIST_OFFSET),
                                1, &value) < 0 || value != first_offset ||
        vart_address_space_read(&address_space, ecam_address(devfn, 0x06), 2,
                                &value) < 0 ||
        !(value & VART_PCI_STATUS_CAPABILITIES) ||
        vart_address_space_read(&address_space, ecam_address(devfn, 0x40), 4,
                                &value) < 0 || value != UINT32_C(0x22114401) ||
        vart_address_space_read(&address_space, ecam_address(devfn, 0x44), 4,
                                &value) < 0 || value != UINT32_C(0x44330005) ||
        vart_pci_device_add_capability(&device, 0x09, NULL, 0, &first_offset) !=
            -EBUSY) {
        fprintf(stderr, "not ok - PCI capability list\n");
        return EXIT_FAILURE;
    }

    vart_device_unrealize(&device.parent);
    if (vart_pci_bus_remove_device(&complex.root_bus, &device) < 0 ||
        vart_pci_root_complex_destroy(&complex) < 0 ||
        vart_bus_destroy(&system_bus) < 0) {
        fprintf(stderr, "not ok - PCI capability teardown\n");
        return EXIT_FAILURE;
    }
    vart_address_space_destroy(&address_space);
    puts("ok - PCI capability list and ECAM access");
    return EXIT_SUCCESS;
}
