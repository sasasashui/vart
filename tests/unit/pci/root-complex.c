#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "vart/pci.h"

int main(void)
{
    VartAddressSpace address_space;
    VartBus system_bus;
    VartPciRootComplex complex;
    VartPciBus child;
    VartPciBus grandchild;
    int ret;

    vart_address_space_init(&address_space);
    vart_bus_init(&system_bus, "system-bus", NULL, &address_space);
    if (vart_pci_root_complex_init(&complex, &system_bus, &address_space) < 0 ||
        complex.root_bus.number != VART_PCI_ROOT_BUS_NUMBER ||
        complex.root_bus.secondary_number != VART_PCI_ROOT_BUS_NUMBER ||
        complex.root_bus.subordinate_number != UINT8_MAX ||
        complex.root_bus.bus.parent != &system_bus ||
        complex.root_bus.bus.address_space != &address_space) {
        fprintf(stderr, "not ok - PCIe root complex lifecycle\n");
        return EXIT_FAILURE;
    }
    ret = vart_pci_bus_init(&child, "pcie.1", &complex.root_bus.bus,
                            &complex.root_bus, &address_space, 1);
    if (ret < 0 || child.parent_pci_bus != &complex.root_bus ||
        child.bus.parent != &complex.root_bus.bus ||
        vart_pci_bus_find(&complex.root_bus, 1) != &child) {
        fprintf(stderr, "not ok - PCIe child bus initialization\n");
        return EXIT_FAILURE;
    }
    ret = vart_pci_bus_init(&grandchild, "pcie.2", &child.bus, &child,
                            &address_space, 2);
    if (ret < 0 || vart_pci_bus_find(&complex.root_bus, 2) != &grandchild ||
        vart_pci_bus_init(&grandchild, "duplicate", &child.bus, &child,
                          &address_space, 2) != -EINVAL ||
        vart_pci_root_complex_destroy(&complex) != -EBUSY ||
        vart_pci_bus_destroy(&grandchild) < 0 ||
        vart_pci_bus_destroy(&child) < 0) {
        fprintf(stderr, "not ok - PCIe bus hierarchy lifecycle\n");
        return EXIT_FAILURE;
    }
    if (vart_bus_add_device(&system_bus, &complex.parent) < 0 ||
        vart_device_realize(&complex.parent) < 0 ||
        !complex.parent.realized || system_bus.device_count != 1 ||
        vart_device_reset(&complex.parent) < 0 ||
        vart_pci_root_complex_destroy(&complex) < 0 ||
        system_bus.device_count != 0 || vart_bus_destroy(&system_bus) < 0) {
        fprintf(stderr, "not ok - PCIe root complex lifecycle\n");
        return EXIT_FAILURE;
    }
    vart_address_space_destroy(&address_space);
    puts("ok - PCIe root complex lifecycle");
    return EXIT_SUCCESS;
}
