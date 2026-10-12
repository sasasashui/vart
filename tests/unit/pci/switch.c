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
    *value = UINT64_C(0xa5);
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
    VartPciSwitch sw;
    VartPciDevice endpoint0;
    VartPciDevice endpoint1;
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
    CHECK(vart_pci_switch_init(&sw, "pci-switch", &complex.root_bus,
                               &address_space, 1, 2) == 0,
          "PCIe switch initialization");
    CHECK(vart_pci_bus_add_device(&complex.root_bus, &sw.upstream.parent, 8) ==
              0,
          "switch upstream attachment");
    CHECK(vart_device_realize(&sw.upstream.parent.parent) == 0,
          "switch upstream realization");
    CHECK(vart_pci_bus_add_device(&sw.upstream.secondary_bus,
                                  &sw.downstream_ports[0].parent, 0) == 0,
          "switch port zero attachment");
    CHECK(vart_pci_bus_add_device(&sw.upstream.secondary_bus,
                                  &sw.downstream_ports[1].parent, 8) == 0,
          "switch port one attachment");
    CHECK(vart_device_realize(&sw.downstream_ports[0].parent.parent) == 0 &&
              vart_device_realize(&sw.downstream_ports[1].parent.parent) == 0,
          "switch downstream realization");
    CHECK(sw.upstream.secondary_bus.subordinate_number == 3,
          "switch subordinate propagation");
    CHECK(vart_pci_device_init(&endpoint0, "switch-endpoint0",
                               UINT16_C(0x1234), UINT16_C(0x1000), 0, 0, 0,
                               0) == 0 &&
              vart_pci_device_init(&endpoint1, "switch-endpoint1",
                                   UINT16_C(0x1234), UINT16_C(0x1001), 0, 0,
                                   0, 0) == 0,
          "switch endpoint initialization");
    CHECK(vart_pci_device_set_bar(&endpoint0, 0, UINT64_C(0x1000), 0,
                                  &test_mmio_ops, NULL) == 0 &&
              vart_pci_device_set_bar(&endpoint1, 0, UINT64_C(0x1000), 0,
                                      &test_mmio_ops, NULL) == 0,
          "switch endpoint BAR setup");
    CHECK(vart_pci_bus_add_device(&sw.downstream_ports[0].secondary_bus,
                                  &endpoint0, 0) == 0 &&
              vart_pci_bus_add_device(&sw.downstream_ports[1].secondary_bus,
                                      &endpoint1, 0) == 0,
          "switch endpoint attachment");
    CHECK(vart_device_realize(&endpoint0.parent) == 0 &&
              vart_device_realize(&endpoint1.parent) == 0,
          "switch endpoint realization");
    CHECK(vart_address_space_read(&address_space, ecam_address(2, 0, 0), 4,
                                  &value) == 0 &&
              value == UINT32_C(0x10001234),
          "switch port zero ECAM routing");
    CHECK(vart_address_space_read(&address_space, ecam_address(3, 0, 0), 4,
                                  &value) == 0 &&
              value == UINT32_C(0x10011234),
          "switch port one ECAM routing");
    CHECK(endpoint0.bars[0].address != endpoint1.bars[0].address &&
              endpoint0.bars[0].address >=
                  sw.downstream_ports[0].mmio_base &&
              endpoint1.bars[0].address >=
                  sw.downstream_ports[1].mmio_base,
          "switch downstream BAR isolation");
    CHECK(vart_address_space_read(&address_space, endpoint0.bars[0].address,
                                  4, &value) == 0 && value == UINT64_C(0xa5) &&
              vart_address_space_read(&address_space, endpoint1.bars[0].address,
                                      4, &value) == 0 &&
              value == UINT64_C(0xa5),
          "switch downstream MMIO dispatch");

    vart_device_unrealize(&endpoint0.parent);
    vart_device_unrealize(&endpoint1.parent);
    vart_device_unrealize(&sw.downstream_ports[0].parent.parent);
    vart_device_unrealize(&sw.downstream_ports[1].parent.parent);
    vart_device_unrealize(&sw.upstream.parent.parent);
    CHECK(vart_pci_bus_remove_device(
              &sw.downstream_ports[0].secondary_bus, &endpoint0) == 0 &&
              vart_pci_bus_remove_device(
                  &sw.downstream_ports[1].secondary_bus, &endpoint1) == 0,
          "switch endpoint removal");
    CHECK(vart_pci_bus_remove_device(&sw.upstream.secondary_bus,
                                     &sw.downstream_ports[0].parent) == 0 &&
              vart_pci_bus_remove_device(&sw.upstream.secondary_bus,
                                         &sw.downstream_ports[1].parent) == 0,
          "switch port removal");
    CHECK(vart_pci_bus_remove_device(&complex.root_bus, &sw.upstream.parent) ==
              0,
          "switch upstream removal");
    CHECK(vart_pci_switch_destroy(&sw) == 0, "PCIe switch destruction");
    CHECK(vart_pci_root_complex_destroy(&complex) == 0,
          "root complex destruction");
    CHECK(vart_bus_destroy(&system_bus) == 0, "system bus destruction");
    vart_address_space_destroy(&address_space);
    puts("ok - PCIe switch topology");
    return EXIT_SUCCESS;
}
