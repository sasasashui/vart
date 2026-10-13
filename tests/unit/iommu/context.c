#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vart/address-space.h"
#include "vart/bus.h"
#include "vart/iommu.h"
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
    uint8_t source[] = "IOMMU context";
    uint8_t result[sizeof(source)] = { 0 };
    VartAddressSpace downstream;
    VartAddressRegion ram;
    VartBus parent_bus;
    VartPciRootComplex complex;
    VartPciDevice device;
    VartIommuContext context;
    uint64_t config_base = VART_VIRT_PCIE_ECAM_BASE + UINT64_C(4);
    int ret;

    vart_address_space_init(&downstream);
    vart_bus_init(&parent_bus, "iommu-parent", NULL, &downstream);
    ret = vart_address_region_init_ram(&ram, VART_VIRT_DRAM_BASE,
                                       sizeof(backing), -1, NULL, backing);
    if (ret < 0 || vart_address_space_add(&downstream, &ram) < 0 ||
        vart_pci_root_complex_init(&complex, &parent_bus, &downstream) < 0 ||
        vart_bus_add_device(&parent_bus, &complex.parent) < 0 ||
        vart_device_realize(&complex.parent) < 0 ||
        vart_pci_device_init(&device, "iommu-context-test", 0x1234, 0x5678,
                             0, 0, 0, 0xff) < 0 ||
        vart_pci_bus_add_device(&complex.root_bus, &device, 0) < 0) {
        return 1;
    }

    ret = vart_iommu_context_init(&context, &downstream,
                                  VART_IOMMU_CONTEXT_IDENTITY);
    if (check(ret == 0, "initialize identity context") < 0 ||
        check(vart_iommu_context_attach(&context, &device) == 0,
              "attach context to PCI device") < 0 ||
        check(vart_iommu_context_matches_requester(
                  &context, vart_pci_device_requester_id(&device)),
              "match attached requester ID") < 0 ||
        check(vart_device_realize(&device.parent) == 0,
              "realize device with attached context") < 0) {
        return 1;
    }
    ret = vart_address_space_write(&downstream, config_base, 2, 0x4);
    if (check(ret == 0, "enable bus mastering") < 0 ||
        check(vart_pci_device_dma_write(&device, VART_VIRT_DRAM_BASE, source,
                                        sizeof(source)) == 0,
              "identity context DMA write") < 0 ||
        check(vart_pci_device_dma_read(&device, VART_VIRT_DRAM_BASE, result,
                                       sizeof(result)) == 0,
              "identity context DMA read") < 0 ||
        check(memcmp(source, result, sizeof(source)) == 0,
              "identity context data round trip") < 0 ||
        check(vart_iommu_context_set_mode(&context,
                                          VART_IOMMU_CONTEXT_FAULT) == 0,
              "switch context to fault mode") < 0 ||
        check(vart_pci_device_dma_read(&device, VART_VIRT_DRAM_BASE, result,
                                       sizeof(result)) == -EACCES,
              "fault context rejects DMA") < 0 ||
        check(vart_iommu_context_detach(&context, &device) == -EINVAL,
              "cannot detach a realized device") < 0) {
        return 1;
    }

    vart_device_unrealize(&device.parent);
    if (check(vart_iommu_context_detach(&context, &device) == 0,
              "detach context after unrealize") < 0 ||
        check(device.parent.dma_as == &downstream &&
                  device.parent.dma_as_from_bus,
              "restore inherited DMA address space") < 0) {
        return 1;
    }
    vart_iommu_context_destroy(&context);
    if (vart_pci_bus_remove_device(&complex.root_bus, &device) < 0 ||
        vart_pci_root_complex_destroy(&complex) < 0) {
        return 1;
    }
    vart_address_space_remove(&downstream, &ram);
    vart_bus_destroy(&parent_bus);
    vart_address_space_destroy(&downstream);
    puts("ok - IOMMU DMA context modes and lifetime");
    return 0;
}
