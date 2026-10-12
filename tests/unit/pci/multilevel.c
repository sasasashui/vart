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
    *value = UINT64_C(0x5a);
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
    VartPciBridge outer;
    VartPciBridge inner;
    VartPciDevice endpoint;
    uint64_t value;

    vart_address_space_init(&address_space);
    vart_bus_init(&system_bus, "system-bus", NULL, &address_space);
    CHECK(vart_pci_root_complex_init(&complex, &system_bus, &address_space) ==
              0,
          "root complex initialization");
    CHECK(vart_bus_add_device(&system_bus, &complex.parent) == 0,
          "root complex attachment");
    CHECK(vart_device_realize(&complex.parent) == 0,
          "root complex realization");
    CHECK(vart_pci_bridge_init(&outer, "outer-bridge", &complex.root_bus,
                               &address_space, 1) == 0,
          "outer bridge initialization");
    CHECK(vart_pci_bus_add_device(&complex.root_bus, &outer.parent, 8) == 0,
          "outer bridge attachment");
    CHECK(vart_device_realize(&outer.parent.parent) == 0,
          "outer bridge realization");
    CHECK(vart_pci_bridge_init(&inner, "inner-bridge", &outer.secondary_bus,
                               &address_space, 2) == 0,
          "inner bridge initialization");
    CHECK(vart_pci_bus_add_device(&outer.secondary_bus, &inner.parent, 0) ==
              0,
          "inner bridge attachment");
    CHECK(vart_device_realize(&inner.parent.parent) == 0,
          "inner bridge realization");
    CHECK(outer.secondary_bus.subordinate_number == 2,
          "outer bridge subordinate propagation");
    CHECK(vart_pci_device_init(&endpoint, "nested-endpoint",
                               UINT16_C(0x1234), UINT16_C(0x5678), 0, 0, 0,
                               0) == 0,
          "nested endpoint initialization");
    CHECK(vart_pci_device_set_bar(&endpoint, 0, UINT64_C(0x1000), 0,
                                  &test_mmio_ops, NULL) == 0,
          "nested endpoint BAR setup");
    CHECK(vart_pci_bus_add_device(&inner.secondary_bus, &endpoint, 0) == 0,
          "nested endpoint attachment");
    CHECK(vart_device_realize(&endpoint.parent) == 0,
          "nested endpoint realization");
    CHECK(vart_address_space_read(&address_space, ecam_address(2, 0, 0), 4,
                                  &value) == 0 &&
              value == UINT32_C(0x56781234),
          "recursive ECAM routing");
    CHECK(vart_address_space_read(&address_space, ecam_address(0, 8, 0x1a),
                                  1, &value) == 0 && value == 2,
          "recursive subordinate bus number");
    CHECK(vart_address_space_read(&address_space, ecam_address(3, 0, 0), 4,
                                  &value) == 0 && value == UINT32_MAX,
          "unassigned downstream bus");
    CHECK(vart_address_space_read(&address_space, endpoint.bars[0].address, 4,
                                  &value) == 0 && value == UINT64_C(0x5a),
          "nested downstream BAR dispatch");

    vart_device_unrealize(&endpoint.parent);
    vart_device_unrealize(&inner.parent.parent);
    vart_device_unrealize(&outer.parent.parent);
    CHECK(vart_pci_bus_remove_device(&inner.secondary_bus, &endpoint) == 0,
          "nested endpoint removal");
    CHECK(vart_pci_bus_remove_device(&outer.secondary_bus, &inner.parent) == 0,
          "inner bridge removal");
    CHECK(vart_pci_bridge_destroy(&inner) == 0, "inner bridge destruction");
    CHECK(vart_pci_bus_remove_device(&complex.root_bus, &outer.parent) == 0,
          "outer bridge removal");
    CHECK(vart_pci_bridge_destroy(&outer) == 0, "outer bridge destruction");
    CHECK(vart_pci_root_complex_destroy(&complex) == 0,
          "root complex destruction");
    CHECK(vart_bus_destroy(&system_bus) == 0, "system bus destruction");
    vart_address_space_destroy(&address_space);
    puts("ok - PCIe multi-level hierarchy");
    return EXIT_SUCCESS;
}
