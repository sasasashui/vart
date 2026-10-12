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

#define CHECK(condition, message)                                             \
    do {                                                                       \
        if (!(condition)) {                                                    \
            fprintf(stderr, "not ok - %s\n", message);                       \
            return EXIT_FAILURE;                                               \
        }                                                                      \
    } while (0)

int main(void)
{
    VartAddressSpace address_space;
    VartBus system_bus;
    VartPciRootComplex complex;
    VartPciBridge bridge;
    VartPciDevice endpoint;
    uint64_t value;

    vart_address_space_init(&address_space);
    vart_bus_init(&system_bus, "system-bus", NULL, &address_space);
    CHECK(vart_pci_root_complex_init(&complex, &system_bus, &address_space) ==
              0,
          "PCIe root complex initialization");
    CHECK(vart_bus_add_device(&system_bus, &complex.parent) == 0,
          "PCIe root complex attachment");
    CHECK(vart_device_realize(&complex.parent) == 0,
          "PCIe root complex realization");
    CHECK(vart_pci_bridge_init(&bridge, "pci-bridge", &complex.root_bus,
                               &address_space, 1) == 0,
          "PCIe bridge initialization");
    CHECK(vart_pci_bus_add_device(&complex.root_bus, &bridge.parent, 8) == 0,
          "PCIe bridge attachment");
    CHECK(vart_device_realize(&bridge.parent.parent) == 0,
          "PCIe bridge realization");
    CHECK(vart_pci_device_init(&endpoint, "pci-endpoint", UINT16_C(0x1234),
                               UINT16_C(0x5678), 1, 2, 3, 4) == 0,
          "PCIe endpoint initialization");
    CHECK(vart_pci_bus_add_device(&bridge.secondary_bus, &endpoint, 0) == 0,
          "PCIe endpoint attachment");
    CHECK(vart_device_realize(&endpoint.parent) == 0,
          "PCIe endpoint realization");
    CHECK(vart_address_space_read(&address_space, ecam_address(1, 0, 0), 4,
                                  &value) == 0 &&
              value == UINT32_C(0x56781234),
          "downstream ECAM routing");
    CHECK(vart_address_space_read(&address_space, ecam_address(0, 8, 0x18),
                                  4, &value) == 0,
          "bridge bus-number configuration read");
    CHECK(value == UINT32_C(0x00010100),
          "bridge bus-number configuration value");
    CHECK(vart_address_space_write(&address_space,
                                  ecam_address(0, 8, 0x19), 2,
                                  UINT16_C(0x0202)) == 0 &&
              bridge.secondary_bus.number == 2 &&
              bridge.secondary_bus.subordinate_number == 2,
          "bridge bus-number update");
    CHECK(vart_address_space_read(&address_space, ecam_address(2, 0, 0), 4,
                                  &value) == 0 &&
              value == UINT32_C(0x56781234),
          "updated downstream ECAM routing");
    CHECK(vart_address_space_read(&address_space, ecam_address(1, 0, 0), 4,
                                  &value) == 0 && value == UINT32_MAX,
          "old downstream bus number removal");
    CHECK(vart_address_space_write(&address_space,
                                  ecam_address(0, 8, 0x19), 2,
                                  UINT16_C(0x0000)) == -EINVAL,
          "invalid bridge bus-number update");

    vart_device_unrealize(&endpoint.parent);
    vart_device_unrealize(&bridge.parent.parent);
    CHECK(vart_pci_bus_remove_device(&bridge.secondary_bus, &endpoint) == 0,
          "PCIe endpoint removal");
    CHECK(vart_pci_bus_remove_device(&complex.root_bus, &bridge.parent) == 0,
          "PCIe bridge removal");
    CHECK(vart_pci_bridge_destroy(&bridge) == 0, "PCIe bridge destruction");
    CHECK(vart_pci_root_complex_destroy(&complex) == 0,
          "PCIe root complex destruction");
    CHECK(vart_bus_destroy(&system_bus) == 0, "system bus destruction");
    vart_address_space_destroy(&address_space);
    puts("ok - PCIe bridge configuration and routing");
    return EXIT_SUCCESS;
}
