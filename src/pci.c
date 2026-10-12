#include <errno.h>
#include <string.h>

#include "vart/machine/virt.h"
#include "vart/pci.h"

static uint64_t config_load_le(const uint8_t *data, unsigned int size)
{
    uint64_t value = 0;
    unsigned int i;

    for (i = 0; i < size; i++) {
        value |= (uint64_t)data[i] << (i * 8);
    }
    return value;
}

static void config_store_le(uint8_t *data, unsigned int size, uint64_t value)
{
    unsigned int i;

    for (i = 0; i < size; i++) {
        data[i] = value >> (i * 8);
    }
}

static VartPciDevice *pci_ecam_lookup(VartPciRootComplex *complex,
                                       uint64_t offset, unsigned int size,
                                       unsigned int *config_offset)
{
    unsigned int bus_number = offset >> 20;
    unsigned int devfn = (offset >> 12) & 0xff;
    unsigned int register_offset = offset & 0xfff;

    if (register_offset > VART_PCI_CONFIG_SPACE_SIZE - size) {
        return NULL;
    }
    *config_offset = register_offset;
    if (bus_number != complex->root_bus.number ||
        !complex->root_bus.functions[devfn] ||
        !complex->root_bus.functions[devfn]->parent.realized) {
        return NULL;
    }
    return complex->root_bus.functions[devfn];
}

static int pci_ecam_read(void *opaque, uint64_t offset, unsigned int size,
                         uint64_t *value)
{
    VartPciRootComplex *complex = opaque;
    VartPciDevice *device;
    unsigned int config_offset;

    device = pci_ecam_lookup(complex, offset, size, &config_offset);
    if (device == NULL) {
        *value = UINT64_MAX;
        return 0;
    }
    *value = config_load_le(&device->config_space[config_offset], size);
    return 0;
}

static int pci_ecam_write(void *opaque, uint64_t offset, unsigned int size,
                          uint64_t value)
{
    VartPciRootComplex *complex = opaque;
    VartPciDevice *device;
    unsigned int config_offset;

    device = pci_ecam_lookup(complex, offset, size, &config_offset);
    if (device != NULL) {
        config_store_le(&device->config_space[config_offset], size, value);
    }
    return 0;
}

static const VartMmioOps pci_ecam_ops = {
    .read = pci_ecam_read,
    .write = pci_ecam_write,
};

static int pci_root_complex_realize(VartDevice *device)
{
    VartPciRootComplex *complex = device->opaque;
    int ret;

    if (device->bus == NULL || complex->root_bus.bus.parent != device->bus ||
        device->bus->address_space != complex->root_bus.bus.address_space) {
        return -EINVAL;
    }
    ret = vart_address_space_add(device->bus->address_space,
                                 &complex->ecam_region);
    if (ret < 0) {
        return ret;
    }
    complex->ecam_mapped = true;
    return 0;
}

static void pci_root_complex_unrealize(VartDevice *device)
{
    VartPciRootComplex *complex = device->opaque;

    if (complex->ecam_mapped) {
        vart_address_space_remove(complex->ecam_region.address_space,
                                  &complex->ecam_region);
        complex->ecam_mapped = false;
    }
}

static const VartDeviceOps pci_root_complex_ops = {
    .realize = pci_root_complex_realize,
    .unrealize = pci_root_complex_unrealize,
};

static int pci_device_realize(VartDevice *device)
{
    VartPciDevice *pci_device = device->opaque;

    if (pci_device->pci_bus == NULL ||
        device->bus != &pci_device->pci_bus->bus) {
        return -EINVAL;
    }
    return 0;
}

static const VartDeviceOps pci_device_ops = {
    .realize = pci_device_realize,
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
    return vart_address_region_init_mmio(&complex->ecam_region,
                                         VART_VIRT_PCIE_ECAM_BASE,
                                         VART_VIRT_PCIE_ECAM_SIZE, 0,
                                         complex, &pci_ecam_ops, complex);
}

int vart_pci_root_complex_destroy(VartPciRootComplex *complex)
{
    int ret;

    if (complex == NULL) {
        return -EINVAL;
    }
    if (complex->root_bus.bus.device_count != 0) {
        return -EBUSY;
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

int vart_pci_device_init(VartPciDevice *device, const char *name,
                         uint16_t vendor_id, uint16_t device_id,
                         uint8_t revision, uint8_t prog_if,
                         uint8_t subclass, uint8_t class_code)
{
    if (device == NULL) {
        return -EINVAL;
    }
    memset(device, 0, sizeof(*device));
    vart_device_init(&device->parent, name, &pci_device_ops, device, NULL);
    device->config_space[0x00] = vendor_id;
    device->config_space[0x01] = vendor_id >> 8;
    device->config_space[0x02] = device_id;
    device->config_space[0x03] = device_id >> 8;
    device->config_space[0x08] = revision;
    device->config_space[0x09] = prog_if;
    device->config_space[0x0a] = subclass;
    device->config_space[0x0b] = class_code;
    return 0;
}

int vart_pci_bus_add_device(VartPciBus *bus, VartPciDevice *device,
                            uint8_t devfn)
{
    int ret;

    if (bus == NULL || device == NULL) {
        return -EINVAL;
    }
    if (device->pci_bus != NULL) {
        return -EALREADY;
    }
    if (bus->functions[devfn] != NULL) {
        return -EEXIST;
    }
    ret = vart_bus_add_device(&bus->bus, &device->parent);
    if (ret < 0) {
        return ret;
    }
    device->pci_bus = bus;
    device->devfn = devfn;
    bus->functions[devfn] = device;
    return 0;
}

int vart_pci_bus_remove_device(VartPciBus *bus, VartPciDevice *device)
{
    int ret;

    if (bus == NULL || device == NULL || device->pci_bus != bus ||
        bus->functions[device->devfn] != device) {
        return -ENOENT;
    }
    if (device->parent.realized) {
        return -EBUSY;
    }
    ret = vart_bus_remove_device(&bus->bus, &device->parent);
    if (ret < 0) {
        return ret;
    }
    bus->functions[device->devfn] = NULL;
    device->pci_bus = NULL;
    return 0;
}
