#include <stdio.h>
#include <stdlib.h>

#include "vart/pci.h"

int main(void)
{
    VartAddressSpace address_space;
    VartBus system_bus;
    VartPciRootComplex complex;

    vart_address_space_init(&address_space);
    vart_bus_init(&system_bus, "system-bus", NULL, &address_space);
    if (vart_pci_root_complex_init(&complex, &system_bus, &address_space) < 0 ||
        complex.root_bus.number != VART_PCI_ROOT_BUS_NUMBER ||
        complex.root_bus.bus.parent != &system_bus ||
        complex.root_bus.bus.address_space != &address_space ||
        vart_bus_add_device(&system_bus, &complex.parent) < 0 ||
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
