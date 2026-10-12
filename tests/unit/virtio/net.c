#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "vart/address-space.h"
#include "vart/bus.h"
#include "vart/machine/virt.h"
#include "vart/virtio-net.h"

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
    static const uint8_t mac[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
    VartAddressSpace address_space;
    VartBus parent_bus;
    VartPciRootComplex complex;
    VartVirtioNetDevice device;
    uint64_t bar_base;
    uint64_t value;
    int ret;

    vart_address_space_init(&address_space);
    vart_bus_init(&parent_bus, "test-parent", NULL, &address_space);
    ret = vart_pci_root_complex_init(&complex, &parent_bus, &address_space);
    if (ret < 0 || vart_bus_add_device(&parent_bus, &complex.parent) < 0 ||
        vart_device_realize(&complex.parent) < 0) {
        return EXIT_FAILURE;
    }
    ret = vart_virtio_net_init(&device, mac, &address_space);
    if (ret == 0) {
        ret = vart_pci_bus_add_device(&complex.root_bus, &device.pci.pci, 0);
    }
    if (ret == 0) {
        ret = vart_device_realize(&device.pci.pci.parent);
    }
    if (ret < 0) {
        vart_pci_root_complex_destroy(&complex);
        vart_bus_destroy(&parent_bus);
        vart_address_space_destroy(&address_space);
        return EXIT_FAILURE;
    }
    bar_base = device.pci.pci.bars[0].address;
    if (check(vart_address_space_read(&address_space,
                                      VART_VIRT_PCIE_ECAM_BASE + 0x00, 4,
                                      &value) == 0 &&
              value == UINT64_C(0x10411af4), "virtio-net PCI identity") < 0 ||
        check(vart_address_space_read(&address_space,
                                      VART_VIRT_PCIE_ECAM_BASE + 0x0b, 1,
                                      &value) == 0 && value == 0x02,
              "virtio-net PCI class") < 0 ||
        check(vart_address_space_read(&address_space, bar_base + 0x300, 4,
                                      &value) == 0 && value == 0x12005452,
              "read virtio-net MAC low") < 0 ||
        check(vart_address_space_read(&address_space, bar_base + 0x304, 2,
                                      &value) == 0 && value == 0x5634,
              "read virtio-net MAC high") < 0 ||
        check(vart_address_space_read(&address_space, bar_base + 0x306, 2,
                                      &value) == 0 && value == 1,
              "read virtio-net link status") < 0 ||
        check(device.pci.pci.msix.configured &&
              device.pci.pci.msix.vector_count == 3,
              "configure virtio-net MSI-X") < 0 ||
        check(vart_address_space_write(&address_space, bar_base + 0x100, 2,
                                       1) == 0,
              "notify virtio-net queue") < 0) {
        vart_device_unrealize(&device.pci.pci.parent);
        vart_pci_bus_remove_device(&complex.root_bus, &device.pci.pci);
        vart_virtio_net_destroy(&device);
        vart_pci_root_complex_destroy(&complex);
        vart_bus_destroy(&parent_bus);
        vart_address_space_destroy(&address_space);
        return EXIT_FAILURE;
    }
    vart_device_unrealize(&device.pci.pci.parent);
    if (vart_pci_bus_remove_device(&complex.root_bus, &device.pci.pci) < 0 ||
        vart_virtio_net_destroy(&device) < 0 ||
        vart_pci_root_complex_destroy(&complex) < 0) {
        return EXIT_FAILURE;
    }
    vart_bus_destroy(&parent_bus);
    vart_address_space_destroy(&address_space);
    puts("ok - virtio-net PCI device configuration");
    return EXIT_SUCCESS;
}
