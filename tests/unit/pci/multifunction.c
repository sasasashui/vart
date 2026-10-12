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

static int check(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "not ok - %s\n", message);
        return EXIT_FAILURE;
    }
    return 0;
}

int main(void)
{
    VartAddressSpace address_space;
    VartBus system_bus;
    VartPciRootComplex complex;
    VartPciDevice function0;
    VartPciDevice function1;
    VartPciDevice duplicate;
    uint64_t value;

    vart_address_space_init(&address_space);
    vart_bus_init(&system_bus, "system-bus", NULL, &address_space);
    if (check(vart_pci_root_complex_init(&complex, &system_bus,
                                        &address_space) == 0,
              "PCIe root complex initialization") != 0 ||
        check(vart_bus_add_device(&system_bus, &complex.parent) == 0,
              "PCIe root complex attachment") != 0 ||
        check(vart_device_realize(&complex.parent) == 0,
              "PCIe root complex realization") != 0 ||
        check(vart_pci_device_init(&function0, "pci-function0", 0x1234,
                                   0x5678, 0, 0, 0, 2) == 0,
              "function zero initialization") != 0 ||
        check(vart_pci_device_init(&function1, "pci-function1", 0x1234,
                                   0x5679, 0, 0, 0, 2) == 0,
              "function one initialization") != 0) {
        return EXIT_FAILURE;
    }
    if (check(vart_pci_bus_add_device(&complex.root_bus, &function1, 9) ==
                  -EINVAL,
              "secondary function requires function zero") != 0 ||
        check(vart_pci_bus_add_device(&complex.root_bus, &function0, 8) == 0,
              "function zero attachment") != 0 ||
        check(vart_pci_bus_add_device(&complex.root_bus, &function1, 9) == 0,
              "secondary function attachment") != 0 ||
        check(vart_device_realize(&function0.parent) == 0 &&
                  vart_device_realize(&function1.parent) == 0,
              "multifunction realization") != 0 ||
        check(vart_address_space_read(&address_space,
                                      ecam_address(8, 0x0e), 1, &value) == 0 &&
                  value == VART_PCI_HEADER_TYPE_MULTIFUNCTION,
              "multifunction header bit") != 0 ||
        check(vart_address_space_read(&address_space,
                                      ecam_address(9, 0), 4, &value) == 0 &&
                  value == UINT32_C(0x56791234),
              "secondary function ECAM access") != 0 ||
        check(vart_pci_device_init(&duplicate, "pci-duplicate", 0x1234,
                                   0x5680, 0, 0, 0, 2) == 0 &&
                  vart_pci_bus_add_device(&complex.root_bus, &duplicate, 9) ==
                      -EEXIST,
              "duplicate function rejection") != 0) {
        return EXIT_FAILURE;
    }

    vart_device_unrealize(&function1.parent);
    if (check(vart_pci_bus_remove_device(&complex.root_bus, &function0) ==
                  -EBUSY,
              "function zero remains while secondary functions exist") != 0 ||
        check(vart_pci_bus_remove_device(&complex.root_bus, &function1) == 0,
              "secondary function removal") != 0 ||
        check(vart_address_space_read(&address_space,
                                      ecam_address(8, 0x0e), 1, &value) == 0 &&
                  value == 0,
              "multifunction header bit cleanup") != 0) {
        return EXIT_FAILURE;
    }
    vart_device_unrealize(&function0.parent);
    if (check(vart_pci_bus_remove_device(&complex.root_bus, &function0) == 0,
              "function zero removal") != 0 ||
        check(vart_pci_root_complex_destroy(&complex) == 0,
              "PCIe root complex destruction") != 0 ||
        check(vart_bus_destroy(&system_bus) == 0,
              "system bus destruction") != 0) {
        return EXIT_FAILURE;
    }
    vart_address_space_destroy(&address_space);
    puts("ok - PCI multifunction enumeration and lifecycle");
    return EXIT_SUCCESS;
}
