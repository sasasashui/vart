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

static VartPciBar *pci_bar_from_offset(VartPciDevice *device,
                                       unsigned int offset, bool *high)
{
    unsigned int index;

    if (offset < 0x10 || offset >= 0x28) {
        return NULL;
    }
    index = (offset - 0x10) / 4;
    if (device->bars[index].high_part) {
        index--;
        *high = true;
    } else {
        *high = false;
    }
    return device->bars[index].implemented ? &device->bars[index] : NULL;
}

static uint64_t pci_bar_probe_value(const VartPciBar *bar, bool high)
{
    uint64_t mask = ~(bar->size - 1);

    if (high) {
        return bar->is_64bit ? mask >> 32 : 0;
    }
    return (uint32_t)mask | bar->flags;
}

static void pci_bar_update_config(VartPciDevice *device, unsigned int index)
{
    VartPciBar *bar = &device->bars[index];
    unsigned int offset = 0x10 + index * 4;

    config_store_le(&device->config_space[offset], 4,
                    (uint32_t)bar->address | bar->flags);
    if (bar->is_64bit) {
        config_store_le(&device->config_space[offset + 4], 4,
                        bar->address >> 32);
    }
}

static int pci_bar_map(VartPciDevice *device, unsigned int index,
                       uint64_t address)
{
    VartPciBar *bar = &device->bars[index];
    VartPciBus *bus = device->pci_bus;
    uint64_t last;
    int ret;

    if (address % bar->size != 0 || address < bus->mmio_base ||
        address > UINT64_MAX - (bar->size - 1)) {
        return -EINVAL;
    }
    last = address + bar->size - 1;
    if (last >= bus->mmio_base + bus->mmio_size) {
        return -ERANGE;
    }
    ret = vart_address_region_init_mmio(&bar->region, address, bar->size, 0,
                                        device, bar->mmio_ops,
                                        bar->mmio_opaque);
    if (ret < 0) {
        return ret;
    }
    ret = vart_address_space_add(bus->bus.address_space, &bar->region);
    if (ret < 0) {
        return ret;
    }
    bar->address = address;
    bar->mapped = true;
    return 0;
}

static void pci_bar_unmap(VartPciDevice *device, unsigned int index)
{
    VartPciBar *bar = &device->bars[index];

    if (bar->mapped) {
        vart_address_space_remove(bar->region.address_space, &bar->region);
        bar->mapped = false;
    }
}

static int pci_bar_relocate(VartPciDevice *device, unsigned int index,
                            uint64_t address)
{
    VartPciBar *bar = &device->bars[index];
    uint64_t old_address = bar->address;
    bool old_mapped = bar->mapped;
    int ret;

    pci_bar_unmap(device, index);
    ret = pci_bar_map(device, index, address);
    if (ret < 0) {
        if (old_mapped) {
            pci_bar_map(device, index, old_address);
        }
        return ret;
    }
    pci_bar_update_config(device, index);
    return 0;
}

static int pci_bar_write(VartPciDevice *device, unsigned int index,
                         bool high, uint32_t value)
{
    VartPciBar *bar = &device->bars[index];
    uint64_t address;

    if (value == UINT32_MAX) {
        bar->probe = true;
        return 0;
    }
    bar->probe = false;
    if (high) {
        address = (bar->address & UINT32_MAX) | ((uint64_t)value << 32);
    } else {
        address = (bar->address & UINT64_C(0xffffffff00000000)) |
                  (value & UINT32_C(0xfffffff0));
    }
    return pci_bar_relocate(device, index, address);
}

static int pci_config_read(VartPciDevice *device, unsigned int offset,
                           unsigned int size, uint64_t *value)
{
    VartPciBar *bar;
    bool high;
    int ret;

    bar = size == 4 ? pci_bar_from_offset(device, offset, &high) : NULL;
    if (bar != NULL && bar->probe) {
        *value = pci_bar_probe_value(bar, high);
        return 0;
    }
    if (bar == NULL && device->config_ops != NULL &&
        device->config_ops->read != NULL) {
        ret = device->config_ops->read(device, offset, size, value);
        if (ret != -ENOENT) {
            return ret;
        }
    }
    *value = config_load_le(&device->config_space[offset], size);
    return 0;
}

static int pci_config_write(VartPciDevice *device, unsigned int offset,
                            unsigned int size, uint64_t value)
{
    VartPciBar *bar;
    bool high;
    int ret;

    bar = size == 4 ? pci_bar_from_offset(device, offset, &high) : NULL;
    if (bar != NULL) {
        return pci_bar_write(device, (unsigned int)(bar - device->bars), high,
                             value);
    }
    if (device->config_ops != NULL && device->config_ops->write != NULL) {
        ret = device->config_ops->write(device, offset, size, value);
        if (ret != -ENOENT) {
            return ret;
        }
    }
    config_store_le(&device->config_space[offset], size, value);
    return 0;
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
    return pci_config_read(device, config_offset, size, value);
}

static int pci_ecam_write(void *opaque, uint64_t offset, unsigned int size,
                          uint64_t value)
{
    VartPciRootComplex *complex = opaque;
    VartPciDevice *device;
    unsigned int config_offset;

    device = pci_ecam_lookup(complex, offset, size, &config_offset);
    if (device != NULL) {
        return pci_config_write(device, config_offset, size, value);
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
    VartPciBus *bus = pci_device->pci_bus;
    uint64_t old_mmio_next;
    unsigned int i;
    int ret;

    if (bus == NULL || device->bus != &bus->bus) {
        return -EINVAL;
    }
    old_mmio_next = bus->mmio_next;
    for (i = 0; i < VART_PCI_BAR_COUNT; i++) {
        VartPciBar *bar = &pci_device->bars[i];
        uint64_t address;

        if (!bar->implemented || bar->high_part) {
            continue;
        }
        address = (bus->mmio_next + bar->size - 1) & ~(bar->size - 1);
        if (address < bus->mmio_next ||
            address > UINT64_MAX - (bar->size - 1)) {
            ret = -EOVERFLOW;
            goto fail;
        }
        if (address + bar->size > bus->mmio_base + bus->mmio_size) {
            ret = -ERANGE;
            goto fail;
        }
        ret = pci_bar_map(pci_device, i, address);
        if (ret < 0) {
            goto fail;
        }
        bus->mmio_next = address + bar->size;
        pci_bar_update_config(pci_device, i);
    }
    return 0;

fail:
    for (i = 0; i < VART_PCI_BAR_COUNT; i++) {
        if (pci_device->bars[i].mapped) {
            pci_bar_unmap(pci_device, i);
        }
    }
    bus->mmio_next = old_mmio_next;
    return ret;
}

static void pci_device_unrealize(VartDevice *device)
{
    VartPciDevice *pci_device = device->opaque;
    unsigned int i;

    for (i = 0; i < VART_PCI_BAR_COUNT; i++) {
        pci_bar_unmap(pci_device, i);
    }
}

static const VartDeviceOps pci_device_ops = {
    .realize = pci_device_realize,
    .unrealize = pci_device_unrealize,
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
    complex->root_bus.mmio_base = VART_VIRT_PCIE_MMIO_BASE;
    complex->root_bus.mmio_size = VART_VIRT_PCIE_MMIO_SIZE;
    complex->root_bus.mmio_next = VART_VIRT_PCIE_MMIO_BASE;
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

int vart_pci_device_set_bar(VartPciDevice *device, unsigned int index,
                            uint64_t size, uint32_t flags,
                            const VartMmioOps *mmio_ops, void *mmio_opaque)
{
    VartPciBar *bar;

    if (device == NULL || index >= VART_PCI_BAR_COUNT || size == 0 ||
        (size & (size - 1)) != 0 || mmio_ops == NULL ||
        (mmio_ops->read == NULL && mmio_ops->write == NULL) ||
        (flags & ~(VART_PCI_BAR_MEM_64 | VART_PCI_BAR_PREFETCHABLE)) != 0) {
        return -EINVAL;
    }
    if (device->parent.realized || device->pci_bus != NULL ||
        (flags & VART_PCI_BAR_MEM_64 && index == VART_PCI_BAR_COUNT - 1)) {
        return -EBUSY;
    }
    bar = &device->bars[index];
    if (bar->implemented || bar->high_part ||
        (flags & VART_PCI_BAR_MEM_64 &&
         (device->bars[index + 1].implemented ||
          device->bars[index + 1].high_part))) {
        return -EEXIST;
    }
    if (!(flags & VART_PCI_BAR_MEM_64) && size > UINT32_MAX) {
        return -ERANGE;
    }
    memset(bar, 0, sizeof(*bar));
    bar->size = size;
    bar->flags = flags;
    bar->mmio_ops = mmio_ops;
    bar->mmio_opaque = mmio_opaque;
    bar->implemented = true;
    bar->is_64bit = flags & VART_PCI_BAR_MEM_64;
    if (bar->is_64bit) {
        device->bars[index + 1].high_part = true;
    }
    return 0;
}

int vart_pci_device_set_config_ops(VartPciDevice *device,
                                   const VartPciConfigOps *config_ops)
{
    if (device == NULL) {
        return -EINVAL;
    }
    if (device->parent.realized || device->pci_bus != NULL) {
        return -EBUSY;
    }
    if (config_ops != NULL && config_ops->read == NULL &&
        config_ops->write == NULL) {
        return -EINVAL;
    }
    device->config_ops = config_ops;
    return 0;
}
