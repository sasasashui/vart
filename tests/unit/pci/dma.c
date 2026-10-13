#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vart/address-space.h"
#include "vart/bus.h"
#include "vart/machine/virt.h"
#include "vart/pci.h"

static int check(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "not ok - %s\n", message);
        return -1;
    }
    return 0;
}

int main(void)
{
    uint8_t backing[0x1000] = { 0 };
    uint8_t source[] = "VART DMA";
    uint8_t result[sizeof(source)] = { 0 };
    VartAddressSpace address_space;
    VartAddressSpace alternate_address_space;
    VartAddressSpace explicit_address_space;
    VartAddressRegion ram;
    VartAddressRegion alternate_ram;
    VartBus parent_bus;
    VartBus alternate_parent_bus;
    VartPciRootComplex complex;
    VartPciBus alternate_bus;
    VartPciDevice device;
    uint64_t config_base;
    int ret;

    vart_address_space_init(&address_space);
    vart_bus_init(&parent_bus, "test-parent", NULL, &address_space);
    ret = vart_address_region_init_ram(&ram, VART_VIRT_DRAM_BASE,
                                       sizeof(backing), -1, NULL, backing);
    if (ret < 0 || vart_address_space_add(&address_space, &ram) < 0) {
        return 1;
    }
    ret = vart_pci_root_complex_init(&complex, &parent_bus, &address_space);
    if (ret < 0 || vart_bus_add_device(&parent_bus, &complex.parent) < 0 ||
        vart_device_realize(&complex.parent) < 0) {
        return 1;
    }
    ret = vart_pci_device_init(&device, "dma-test", 0x1234, 0x5678, 0, 0,
                               0, 0xff);
    if (ret < 0 || vart_pci_bus_add_device(&complex.root_bus, &device, 0) < 0 ||
        vart_device_realize(&device.parent) < 0) {
        return 1;
    }

    config_base = VART_VIRT_PCIE_ECAM_BASE + UINT64_C(4);
    if (check(vart_pci_device_dma_read(&device, VART_VIRT_DRAM_BASE,
                                       result, sizeof(result)) == -EPERM,
              "DMA is disabled until bus mastering is enabled") < 0 ||
        check(vart_address_space_write(&address_space, config_base, 2, 0x6) ==
                  0,
              "PCI command register write") < 0 ||
        check(device.memory_enabled && device.bus_master_enabled,
              "PCI command state") < 0 ||
        check(vart_pci_device_dma_write(&device, VART_VIRT_DRAM_BASE, source,
                                        sizeof(source)) == 0,
              "DMA write") < 0 ||
        check(vart_pci_device_dma_read(&device, VART_VIRT_DRAM_BASE, result,
                                       sizeof(result)) == 0,
              "DMA read") < 0 ||
        check(vart_pci_device_dma_read_with_requester_id(
                  &device, vart_pci_device_requester_id(&device),
                  VART_VIRT_DRAM_BASE, result, sizeof(result)) == 0,
              "DMA read with requester ID") < 0 ||
        check(vart_pci_device_dma_read_with_requester_id(
                  &device, UINT16_C(0xffff), VART_VIRT_DRAM_BASE, result,
                  sizeof(result)) == -EPERM,
              "DMA rejects an invalid requester ID") < 0 ||
        check(memcmp(source, result, sizeof(source)) == 0,
              "DMA data round trip") < 0 ||
        check(vart_pci_device_dma_read(&device, VART_VIRT_DRAM_BASE +
                                       sizeof(backing), result, 1) == -ENOENT,
              "DMA rejects an unmapped address") < 0 ||
        check(vart_address_space_write(&address_space, config_base, 2, 0x2) ==
                  0,
              "PCI bus mastering disable") < 0 ||
        check(vart_pci_device_dma_write(&device, VART_VIRT_DRAM_BASE, source,
                                        sizeof(source)) == -EPERM,
              "DMA is blocked after bus mastering is disabled") < 0) {
        return 1;
    }

    vart_device_unrealize(&device.parent);
    if (vart_pci_bus_remove_device(&complex.root_bus, &device) < 0) {
        return 1;
    }
    if (check(device.parent.dma_as == NULL &&
                  !device.parent.dma_as_from_bus,
              "remove detaches an inherited DMA address space") < 0) {
        return 1;
    }

    vart_address_space_init(&alternate_address_space);
    vart_bus_init(&alternate_parent_bus, "alternate-parent", NULL,
                  &alternate_address_space);
    ret = vart_address_region_init_ram(&alternate_ram, VART_VIRT_DRAM_BASE,
                                       sizeof(backing), -1, NULL, backing);
    if (ret < 0 || vart_address_space_add(&alternate_address_space,
                                          &alternate_ram) < 0 ||
        vart_pci_bus_init(&alternate_bus, "alternate-pci", &alternate_parent_bus,
                          NULL, &alternate_address_space, 2) < 0 ||
        vart_pci_bus_add_device(&alternate_bus, &device, 0) < 0) {
        return 1;
    }
    if (check(device.parent.dma_as == &alternate_address_space &&
                  device.parent.dma_as_from_bus,
              "re-add inherits the new bus DMA address space") < 0) {
        return 1;
    }
    vart_device_unrealize(&device.parent);
    if (vart_pci_bus_remove_device(&alternate_bus, &device) < 0 ||
        check(device.parent.dma_as == NULL &&
                  !device.parent.dma_as_from_bus,
              "second remove clears the inherited DMA address space") < 0) {
        return 1;
    }

    vart_address_space_init(&explicit_address_space);
    if (check(vart_pci_device_set_dma_as(&device, &explicit_address_space) == 0,
              "set an explicit DMA address space while detached") < 0 ||
        check(device.parent.dma_as == &explicit_address_space &&
                  !device.parent.dma_as_from_bus,
              "explicit DMA address space is marked non-inherited") < 0 ||
        check(vart_pci_bus_add_device(&complex.root_bus, &device, 0) == 0,
              "re-add with an explicit DMA address space") < 0 ||
        check(device.parent.dma_as == &explicit_address_space &&
                  !device.parent.dma_as_from_bus,
              "explicit DMA address space survives bus attachment") < 0) {
        return 1;
    }
    if (vart_pci_bus_remove_device(&complex.root_bus, &device) < 0 ||
        check(device.parent.dma_as == &explicit_address_space &&
                  !device.parent.dma_as_from_bus,
              "explicit DMA address space survives removal") < 0) {
        return 1;
    }
    if (vart_pci_bus_destroy(&alternate_bus) < 0) {
        return 1;
    }
    vart_address_space_remove(&alternate_address_space, &alternate_ram);
    vart_bus_destroy(&alternate_parent_bus);
    vart_address_space_destroy(&alternate_address_space);
    vart_address_space_destroy(&explicit_address_space);
    if (vart_pci_root_complex_destroy(&complex) < 0) {
        return 1;
    }
    vart_address_space_remove(&address_space, &ram);
    vart_bus_destroy(&parent_bus);
    vart_address_space_destroy(&address_space);
    puts("ok - PCI bus mastering and DMA");
    return 0;
}
