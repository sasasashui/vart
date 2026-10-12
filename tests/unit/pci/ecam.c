#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "vart/machine/virt.h"
#include "vart/pci.h"

static uint64_t ecam_address(uint8_t bus, uint8_t devfn, uint16_t offset)
{
    return VART_VIRT_PCIE_ECAM_BASE + ((uint64_t)bus << 20) +
           ((uint64_t)devfn << 12) + offset;
}

int main(void)
{
    VartAddressSpace address_space;
    VartBus system_bus;
    VartPciRootComplex complex;
    VartPciDevice device;
    uint64_t value;
    const uint8_t devfn = 8;

    vart_address_space_init(&address_space);
    vart_bus_init(&system_bus, "system-bus", NULL, &address_space);
    if (vart_pci_root_complex_init(&complex, &system_bus, &address_space) < 0 ||
        vart_bus_add_device(&system_bus, &complex.parent) < 0 ||
        vart_device_realize(&complex.parent) < 0 ||
        vart_address_space_read(&address_space,
                                ecam_address(0, 1, 0), 4, &value) < 0 ||
        value != UINT32_MAX ||
        vart_pci_device_init(&device, "pci-test", UINT16_C(0x1234),
                             UINT16_C(0x5678), 1, 2, 3, 4) < 0 ||
        vart_pci_bus_add_device(&complex.root_bus, &device, devfn) < 0 ||
        vart_device_realize(&device.parent) < 0 ||
        vart_address_space_read(&address_space,
                                ecam_address(0, devfn, 0), 4, &value) < 0 ||
        value != UINT32_C(0x56781234) ||
        vart_address_space_read(&address_space,
                                ecam_address(0, devfn, 8), 4, &value) < 0 ||
        value != UINT32_C(0x04030201) ||
        vart_address_space_write(&address_space,
                                 ecam_address(0, devfn, 4), 2, 7) < 0 ||
        vart_address_space_read(&address_space,
                                ecam_address(0, devfn, 4), 2, &value) < 0 ||
        value != 7 ||
        vart_address_space_read(&address_space,
                                ecam_address(1, devfn, 0), 4, &value) < 0 ||
        value != UINT32_MAX ||
        vart_address_space_read(&address_space,
                                ecam_address(0, devfn, 0xfff), 2, &value) < 0 ||
        value != UINT16_MAX ||
        vart_pci_bus_remove_device(&complex.root_bus, &device) != -EBUSY) {
        fprintf(stderr, "not ok - PCIe ECAM config access\n");
        return EXIT_FAILURE;
    }
    vart_device_unrealize(&device.parent);
    if (vart_pci_bus_remove_device(&complex.root_bus, &device) < 0 ||
        vart_pci_root_complex_destroy(&complex) < 0 ||
        vart_bus_destroy(&system_bus) < 0) {
        fprintf(stderr, "not ok - PCIe ECAM teardown\n");
        return EXIT_FAILURE;
    }
    vart_address_space_destroy(&address_space);
    puts("ok - PCIe ECAM config access");
    return EXIT_SUCCESS;
}
