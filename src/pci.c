#include <errno.h>
#include <string.h>

#include "vart/pci.h"

static int pci_root_complex_realize(VartDevice *device)
{
    VartPciRootComplex *complex = device->opaque;

    if (device->bus == NULL || complex->root_bus.bus.parent != device->bus) {
        return -EINVAL;
    }
    return 0;
}

static const VartDeviceOps pci_root_complex_ops = {
    .realize = pci_root_complex_realize,
};

int vart_pci_root_complex_init(VartPciRootComplex *complex,
                               VartBus *parent_bus,
                               VartAddressSpace *address_space)
{
    if (complex == NULL || parent_bus == NULL || address_space == NULL) {
        return -EINVAL;
    }
    memset(complex, 0, sizeof(*complex));
    vart_device_init(&complex->parent, "pcie-root-complex",
                     &pci_root_complex_ops, complex, NULL);
    vart_bus_init(&complex->root_bus.bus, "pcie.0", parent_bus,
                  address_space);
    complex->root_bus.number = VART_PCI_ROOT_BUS_NUMBER;
    return 0;
}

int vart_pci_root_complex_destroy(VartPciRootComplex *complex)
{
    int ret;

    if (complex == NULL) {
        return -EINVAL;
    }
    vart_device_unrealize(&complex->parent);
    if (complex->parent.bus != NULL) {
        ret = vart_bus_remove_device(complex->parent.bus, &complex->parent);
        if (ret < 0) {
            return ret;
        }
    }
    return vart_bus_destroy(&complex->root_bus.bus);
}
