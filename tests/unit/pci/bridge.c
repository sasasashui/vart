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

static int test_mmio_read(void *opaque, uint64_t offset, unsigned int size,
                          uint64_t *value)
{
    (void)opaque;
    (void)offset;
    (void)size;
    *value = 0;
    return 0;
}

static int test_mmio_write(void *opaque, uint64_t offset, unsigned int size,
                           uint64_t value)
{
    (void)opaque;
    (void)offset;
    (void)size;
    (void)value;
    return 0;
}

static const VartMmioOps test_mmio_ops = {
    .read = test_mmio_read,
    .write = test_mmio_write,
};

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
    VartPciDevice prefetch_endpoint;
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
    CHECK(vart_pci_device_requester_id(&bridge.parent) == UINT16_C(0x0008),
          "bridge requester ID");
    CHECK(vart_pci_device_init(&endpoint, "pci-endpoint", UINT16_C(0x1234),
                               UINT16_C(0x5678), 1, 2, 3, 4) == 0,
          "PCIe endpoint initialization");
    CHECK(vart_pci_device_set_bar(&endpoint, 0, UINT64_C(0x1000), 0,
                                  &test_mmio_ops, NULL) == 0,
          "PCIe endpoint BAR setup");
    CHECK(vart_pci_bus_add_device(&bridge.secondary_bus, &endpoint, 0) == 0,
          "PCIe endpoint attachment");
    CHECK(vart_pci_device_requester_id(&endpoint) == UINT16_C(0x0100),
          "downstream requester ID");
    CHECK(vart_device_realize(&endpoint.parent) == 0,
          "PCIe endpoint realization");
    CHECK(vart_address_space_write(&address_space, ecam_address(1, 0, 0x04),
                                   2, VART_PCI_COMMAND_MEMORY_ENABLE) == 0,
          "downstream BAR memory enablement");
    CHECK(endpoint.bars[0].address >= bridge.mmio_base &&
              endpoint.bars[0].address + endpoint.bars[0].size <=
              bridge.mmio_base + bridge.mmio_size,
          "downstream BAR allocation");
    CHECK(vart_address_space_read(&address_space, endpoint.bars[0].address,
                                  4, &value) == 0 && value == 0,
          "downstream BAR dispatch");
    CHECK(vart_address_space_read(&address_space, ecam_address(1, 0, 0), 4,
                                  &value) == 0 &&
              value == UINT32_C(0x56781234),
          "downstream ECAM routing");
    CHECK(vart_address_space_read(&address_space, ecam_address(0, 8, 0x18),
                                  4, &value) == 0,
          "bridge bus-number configuration read");
    CHECK(value == UINT32_C(0x00010100),
          "bridge bus-number configuration value");
    CHECK(vart_address_space_read(&address_space, ecam_address(0, 8, 0x20),
                                  4, &value) == 0 &&
              value == UINT32_C(0x4fff4000),
          "bridge MMIO window configuration");
    CHECK(vart_address_space_write(&address_space,
                                  ecam_address(0, 8, 0x19), 2,
                                  UINT16_C(0x0202)) == 0 &&
              bridge.secondary_bus.number == 2 &&
              bridge.secondary_bus.subordinate_number == 2 &&
              vart_pci_device_requester_id(&endpoint) == UINT16_C(0x0200),
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
                                  UINT16_C(0x0100)) == -EINVAL,
          "invalid bridge bus-number update");
    CHECK(vart_address_space_write(&address_space,
                                  ecam_address(0, 8, 0x18), 4,
                                  UINT32_C(0x00000000)) == 0,
          "bridge bus-number disable write");
    CHECK(bridge.secondary_bus.number == 2 &&
              bridge.secondary_bus.subordinate_number == 2,
          "bridge topology remains stable while disabled");
    CHECK(vart_address_space_write(&address_space,
                                  ecam_address(0, 8, 0x20), 4,
                                  UINT32_C(0x00000000)) == 0,
          "bridge MMIO window probe write");
    CHECK(vart_address_space_read(&address_space, ecam_address(0, 8, 0x20),
                                  4, &value) == 0 &&
              value == 0 && bridge.mmio_base == 0 &&
              bridge.mmio_size == UINT64_C(0x100000) &&
              bridge.secondary_bus.mmio_size == UINT64_C(0x100000),
          "bridge MMIO window reprogramming");
    CHECK(vart_address_space_write(&address_space,
                                  ecam_address(0, 8, 0x20), 4,
                                  UINT32_C(0x4fff4000)) == 0 &&
              bridge.mmio_base == VART_VIRT_PCIE_MMIO_BASE &&
              bridge.mmio_size == VART_PCI_BRIDGE_MMIO_WINDOW_SIZE,
          "bridge MMIO window reprogramming");
    CHECK(vart_address_space_write(&address_space,
                                  ecam_address(0, 8, 0x24), 4,
                                  UINT32_C(0x402f4020)) == 0 &&
              bridge.prefetch_mmio_base == UINT64_C(0x40200000) &&
              bridge.prefetch_mmio_size == UINT64_C(0x100000) &&
              bridge.secondary_bus.prefetch_mmio_base ==
                  UINT64_C(0x40200000) &&
              bridge.secondary_bus.prefetch_mmio_size == UINT64_C(0x100000),
          "bridge prefetchable MMIO window reprogramming");
    CHECK(vart_pci_device_init(&prefetch_endpoint, "pci-prefetch-endpoint",
                               UINT16_C(0x1234), UINT16_C(0x5679), 1, 2, 3,
                               4) == 0,
          "prefetchable endpoint initialization");
    CHECK(vart_pci_device_set_bar(&prefetch_endpoint, 0, UINT64_C(0x2000),
                                  VART_PCI_BAR_PREFETCHABLE, &test_mmio_ops,
                                  NULL) == 0,
          "prefetchable endpoint BAR setup");
    CHECK(vart_pci_bus_add_device(&bridge.secondary_bus, &prefetch_endpoint,
                                  1) == 0,
          "prefetchable endpoint attachment");
    CHECK(vart_device_realize(&prefetch_endpoint.parent) == 0 &&
              prefetch_endpoint.bars[0].address == UINT64_C(0x40200000),
          "prefetchable BAR allocation");
    CHECK(vart_address_space_write(&address_space,
                                   ecam_address(2, 1, 0x04), 2,
                                   VART_PCI_COMMAND_MEMORY_ENABLE) == 0,
          "prefetchable BAR memory enablement");
    CHECK(vart_address_space_read(&address_space,
                                  prefetch_endpoint.bars[0].address, 4,
                                  &value) == 0 && value == 0,
          "prefetchable BAR dispatch");

    vart_device_unrealize(&prefetch_endpoint.parent);
    vart_device_unrealize(&endpoint.parent);
    vart_device_unrealize(&bridge.parent.parent);
    CHECK(vart_pci_bus_remove_device(&bridge.secondary_bus,
                                    &prefetch_endpoint) == 0,
          "prefetchable endpoint removal");
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
