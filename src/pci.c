#include <errno.h>
#include <stdlib.h>
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

static bool pci_bus_window_contains(const VartPciBus *bus, bool prefetch,
                                    uint64_t address, uint64_t last)
{
    while (bus != NULL) {
        uint64_t base = prefetch ? bus->prefetch_mmio_base : bus->mmio_base;
        uint64_t size = prefetch ? bus->prefetch_mmio_size : bus->mmio_size;

        if (size != 0 && base <= UINT64_MAX - size &&
            address >= base && last < base + size) {
            return true;
        }
        bus = bus->parent_pci_bus;
    }
    return false;
}

static int pci_bar_map(VartPciDevice *device, unsigned int index,
                       uint64_t address)
{
    VartPciBar *bar = &device->bars[index];
    VartPciBus *bus = device->pci_bus;
    bool prefetch = (bar->flags & VART_PCI_BAR_PREFETCHABLE) != 0;
    uint64_t last;
    int ret;

    if (address % bar->size != 0 ||
        address > UINT64_MAX - (bar->size - 1)) {
        return -EINVAL;
    }
    last = address + bar->size - 1;
    if (!pci_bus_window_contains(bus, prefetch, address, last)) {
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

static void pci_msix_flush_pending(VartPciDevice *device);

static int pci_msix_table_read(void *opaque, uint64_t offset,
                               unsigned int size, uint64_t *value)
{
    VartPciDevice *device = opaque;
    VartPciMsix *msix = &device->msix;
    uint64_t table_size = (uint64_t)msix->vector_count * 16;

    if (offset > table_size || size > table_size - offset) {
        return -EINVAL;
    }
    *value = config_load_le(&msix->table[offset], size);
    return 0;
}

static int pci_msix_table_write(void *opaque, uint64_t offset,
                                unsigned int size, uint64_t value)
{
    VartPciDevice *device = opaque;
    VartPciMsix *msix = &device->msix;
    uint64_t table_size = (uint64_t)msix->vector_count * 16;

    if (offset > table_size || size > table_size - offset) {
        return -EINVAL;
    }
    config_store_le(&msix->table[offset], size, value);
    pci_msix_flush_pending(device);
    return 0;
}

static int pci_msix_pba_read(void *opaque, uint64_t offset,
                             unsigned int size, uint64_t *value)
{
    VartPciDevice *device = opaque;
    VartPciMsix *msix = &device->msix;
    uint64_t pba_size = ((msix->vector_count + 63) / 64) * 8;

    if (offset > pba_size || size > pba_size - offset) {
        return -EINVAL;
    }
    *value = config_load_le(&msix->pba[offset], size);
    return 0;
}

static int pci_msix_pba_write(void *opaque, uint64_t offset,
                              unsigned int size, uint64_t value)
{
    VartPciDevice *device = opaque;
    VartPciMsix *msix = &device->msix;
    uint64_t pba_size = ((msix->vector_count + 63) / 64) * 8;

    (void)value;
    if (offset > pba_size || size > pba_size - offset) {
        return -EINVAL;
    }
    return 0;
}

static const VartMmioOps pci_msix_table_ops = {
    .read = pci_msix_table_read,
    .write = pci_msix_table_write,
};

static const VartMmioOps pci_msix_pba_ops = {
    .read = pci_msix_pba_read,
    .write = pci_msix_pba_write,
};

static bool pci_msix_uses_bar(const VartPciDevice *device, unsigned int index)
{
    return device->msix.configured &&
           (device->msix.table_bar == index || device->msix.pba_bar == index);
}

static void pci_msix_unmap(VartPciDevice *device)
{
    VartPciMsix *msix = &device->msix;

    if (!msix->mapped) {
        return;
    }
    vart_address_space_remove(msix->table_region.address_space,
                              &msix->table_region);
    vart_address_space_remove(msix->pba_region.address_space,
                              &msix->pba_region);
    msix->mapped = false;
}

static int pci_msix_map(VartPciDevice *device)
{
    VartPciMsix *msix = &device->msix;
    VartPciBar *table_bar;
    VartPciBar *pba_bar;
    VartAddressSpace *address_space;
    uint64_t table_size;
    uint64_t pba_size;
    uint64_t table_last;
    uint64_t pba_last;
    int ret;

    if (!msix->configured || msix->mapped) {
        return 0;
    }
    table_size = (uint64_t)msix->vector_count * 16;
    pba_size = ((msix->vector_count + 63) / 64) * 8;
    table_bar = &device->bars[msix->table_bar];
    pba_bar = &device->bars[msix->pba_bar];
    if (!table_bar->implemented || !pba_bar->implemented ||
        msix->table_offset > table_bar->size ||
        table_size > table_bar->size - msix->table_offset ||
        msix->pba_offset > pba_bar->size ||
        pba_size > pba_bar->size - msix->pba_offset) {
        return -ERANGE;
    }
    table_last = msix->table_offset + table_size - 1;
    pba_last = msix->pba_offset + pba_size - 1;
    if (msix->table_bar == msix->pba_bar &&
        msix->table_offset <= pba_last && msix->pba_offset <= table_last) {
        return -EINVAL;
    }
    address_space = device->pci_bus->bus.address_space;
    ret = vart_address_region_init_mmio(
        &msix->table_region, table_bar->address + msix->table_offset,
        table_size, 1, device, &pci_msix_table_ops, device);
    if (ret < 0) {
        return ret;
    }
    ret = vart_address_space_add(address_space, &msix->table_region);
    if (ret < 0) {
        return ret;
    }
    ret = vart_address_region_init_mmio(
        &msix->pba_region, pba_bar->address + msix->pba_offset, pba_size, 1,
        device, &pci_msix_pba_ops, device);
    if (ret < 0) {
        vart_address_space_remove(address_space, &msix->table_region);
        return ret;
    }
    ret = vart_address_space_add(address_space, &msix->pba_region);
    if (ret < 0) {
        vart_address_space_remove(address_space, &msix->table_region);
        return ret;
    }
    msix->mapped = true;
    return 0;
}

static void pci_device_disable_memory(VartPciDevice *device)
{
    unsigned int i;

    pci_msix_unmap(device);
    for (i = 0; i < VART_PCI_BAR_COUNT; i++) {
        pci_bar_unmap(device, i);
    }
}

static int pci_device_enable_memory(VartPciDevice *device)
{
    unsigned int i;
    int ret;

    for (i = 0; i < VART_PCI_BAR_COUNT; i++) {
        VartPciBar *bar = &device->bars[i];

        if (!bar->implemented || bar->high_part || bar->mapped) {
            continue;
        }
        ret = pci_bar_map(device, i, bar->address);
        if (ret < 0) {
            goto fail;
        }
    }
    ret = pci_msix_map(device);
    if (ret < 0) {
        goto fail;
    }
    return 0;

fail:
    pci_device_disable_memory(device);
    return ret;
}

static int pci_msix_deliver(VartPciDevice *device, unsigned int vector)
{
    VartPciMsix *msix = &device->msix;
    uint32_t vector_control;
    int ret;

    if (!msix->enabled || msix->function_mask) {
        return -EAGAIN;
    }
    vector_control = (uint32_t)config_load_le(
        &msix->table[vector * 16 + 12], 4);
    if (vector_control & 1) {
        return -EAGAIN;
    }
    if (msix->irq_routes[vector] == NULL) {
        if (msix->inject == NULL) {
            return -ENOTCONN;
        }
        ret = msix->inject(device, vector, msix->inject_opaque);
    } else {
        ret = vart_irq_pulse(msix->irq_routes[vector]);
    }
    if (ret < 0) {
        return ret;
    }
    msix->pba[vector / 8] &= (uint8_t)~(1U << (vector % 8));
    return 0;
}

static void pci_msix_flush_pending(VartPciDevice *device)
{
    VartPciMsix *msix = &device->msix;
    unsigned int vector;

    if (!msix->configured || !msix->enabled || msix->function_mask) {
        return;
    }
    for (vector = 0; vector < msix->vector_count; vector++) {
        if (msix->pba[vector / 8] & (1U << (vector % 8))) {
            (void)pci_msix_deliver(device, vector);
        }
    }
}

static int pci_bar_relocate(VartPciDevice *device, unsigned int index,
                            uint64_t address)
{
    VartPciBar *bar = &device->bars[index];
    uint64_t old_address = bar->address;
    bool old_mapped = bar->mapped;
    bool msix_mapped = device->msix.mapped && pci_msix_uses_bar(device, index);
    int ret;

    if (!device->memory_enabled) {
        bar->address = address;
        pci_bar_update_config(device, index);
        return 0;
    }

    if (msix_mapped) {
        pci_msix_unmap(device);
    }
    pci_bar_unmap(device, index);
    ret = pci_bar_map(device, index, address);
    if (ret < 0) {
        goto restore;
    }
    if (msix_mapped) {
        ret = pci_msix_map(device);
        if (ret < 0) {
            pci_bar_unmap(device, index);
            goto restore;
        }
    }
    pci_bar_update_config(device, index);
    return 0;

restore:
    if (old_mapped && pci_bar_map(device, index, old_address) == 0 &&
        msix_mapped) {
        pci_msix_map(device);
    }
    return ret;
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

static void pci_msix_update_control(VartPciDevice *device)
{
    VartPciMsix *msix = &device->msix;
    uint16_t control;

    if (!msix->configured) {
        return;
    }
    control = (uint16_t)config_load_le(
        &device->config_space[msix->capability_offset + 2], 2);
    msix->enabled = control & UINT16_C(0x8000);
    msix->function_mask = control & UINT16_C(0x4000);
    pci_msix_flush_pending(device);
}

static bool pci_msix_config_readonly(const VartPciDevice *device,
                                     unsigned int offset)
{
    unsigned int capability_offset = device->msix.capability_offset;

    return device->msix.configured && offset >= capability_offset + 4 &&
           offset < capability_offset + 12;
}

static void pci_config_store(VartPciDevice *device, unsigned int offset,
                             unsigned int size, uint64_t value)
{
    unsigned int i;

    for (i = 0; i < size; i++) {
        if (!pci_msix_config_readonly(device, offset + i)) {
            device->config_space[offset + i] = value >> (i * 8);
        }
    }
}

static int pci_config_write(VartPciDevice *device, unsigned int offset,
                            unsigned int size, uint64_t value)
{
    VartPciBar *bar;
    uint16_t old_command;
    uint16_t command;
    bool command_write;
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
    command_write = offset < 0x06 && offset + size > 0x04;
    old_command = (uint16_t)config_load_le(&device->config_space[0x04], 2);
    pci_config_store(device, offset, size, value);
    if (command_write) {
        command = (uint16_t)config_load_le(&device->config_space[0x04], 2);

        if ((command & VART_PCI_COMMAND_MEMORY_ENABLE) != 0 &&
            (old_command & VART_PCI_COMMAND_MEMORY_ENABLE) == 0) {
            ret = pci_device_enable_memory(device);
            if (ret < 0) {
                pci_config_store(device, 0x04, 2, old_command);
                return ret;
            }
        } else if ((command & VART_PCI_COMMAND_MEMORY_ENABLE) == 0 &&
                   (old_command & VART_PCI_COMMAND_MEMORY_ENABLE) != 0) {
            pci_device_disable_memory(device);
        }

        device->memory_enabled =
            (command & VART_PCI_COMMAND_MEMORY_ENABLE) != 0;
        device->bus_master_enabled = command & VART_PCI_COMMAND_BUS_MASTER;
    }
    pci_msix_update_control(device);
    return 0;
}

static VartPciBus *pci_bus_root(VartPciBus *bus)
{
    while (bus != NULL && bus->parent_pci_bus != NULL) {
        bus = bus->parent_pci_bus;
    }
    return bus;
}

static void pci_bus_update_requester_ids(VartPciBus *bus)
{
    size_t i;

    for (i = 0; i < VART_PCI_MAX_FUNCTIONS; i++) {
        if (bus->functions[i] != NULL) {
            bus->functions[i]->requester_id =
                ((uint16_t)bus->number << 8) | (uint16_t)i;
        }
    }
    for (i = 0; i < bus->child_count; i++) {
        pci_bus_update_requester_ids(bus->children[i]);
    }
}

static void pci_bus_update_ancestors(VartPciBus *bus)
{
    while (bus != NULL && bus->parent_pci_bus != NULL) {
        VartPciBus *parent = bus->parent_pci_bus;

        if (bus->owner_bridge != NULL &&
            bus->owner_bridge->parent.config_space[0x1a] <
            bus->subordinate_number) {
            bus->owner_bridge->parent.config_space[0x1a] =
                bus->subordinate_number;
        }
        if (parent->subordinate_number < bus->subordinate_number) {
            parent->subordinate_number = bus->subordinate_number;
        }
        bus = parent;
    }
}

static bool pci_bus_visible(const VartPciBus *root,
                            const VartPciBus *target)
{
    const VartPciBus *bus = target;

    while (bus != NULL && bus != root) {
        if (bus->parent_pci_bus == NULL ||
            bus->number > bus->subordinate_number) {
            return false;
        }
        bus = bus->parent_pci_bus;
    }
    return bus == root;
}

static int pci_bridge_config_read(VartPciDevice *device, unsigned int offset,
                                  unsigned int size, uint64_t *value)
{
    VartPciBridge *bridge = (VartPciBridge *)device;
    VartPciBus *parent = bridge->secondary_bus.parent_pci_bus;
    uint8_t data[4];

    if (offset >= 0x20 && offset <= 0x23 && size <= 4 - (offset - 0x20)) {
        *value = config_load_le(&device->config_space[offset], size);
        return 0;
    }
    if (offset < 0x18 || offset > 0x1b || size > 4 - (offset - 0x18)) {
        return -ENOENT;
    }
    data[0] = parent != NULL ? parent->number : 0;
    data[1] = bridge->secondary_bus.secondary_number;
    data[2] = bridge->secondary_bus.subordinate_number;
    data[3] = device->config_space[0x1b];
    *value = config_load_le(&data[offset - 0x18], size);
    return 0;
}

static int pci_bridge_update_mmio_window(VartPciBridge *bridge,
                                         uint32_t config)
{
    uint64_t base = ((uint64_t)(config & UINT32_C(0xfff0))) << 16;
    uint64_t limit = (((uint64_t)(config >> 16) & UINT64_C(0xfff0)) << 16) |
                     UINT64_C(0xfffff);

    if (base > limit) {
        bridge->mmio_base = base;
        bridge->mmio_size = 0;
        bridge->secondary_bus.mmio_base = base;
        bridge->secondary_bus.mmio_size = 0;
        return 0;
    }
    bridge->mmio_base = base;
    bridge->mmio_size = limit - base + 1;
    bridge->secondary_bus.mmio_base = base;
    bridge->secondary_bus.mmio_size = bridge->mmio_size;
    return 0;
}

static int pci_bridge_update_prefetch_window(VartPciBridge *bridge)
{
    uint16_t base_low = (uint16_t)config_load_le(
        &bridge->parent.config_space[0x24], 2);
    uint16_t limit_low = (uint16_t)config_load_le(
        &bridge->parent.config_space[0x26], 2);
    uint64_t base = ((uint64_t)(base_low & UINT16_C(0xfff0)) << 16) |
                    ((uint64_t)config_load_le(
                         &bridge->parent.config_space[0x28], 4) << 32);
    uint64_t limit = (((uint64_t)(limit_low & UINT16_C(0xfff0)) << 16) |
                      ((uint64_t)config_load_le(
                           &bridge->parent.config_space[0x2c], 4) << 32) |
                     UINT64_C(0xfffff));

    bridge->prefetch_mmio_base = base;
    bridge->prefetch_mmio_size = base > limit ? 0 : limit - base + 1;
    bridge->secondary_bus.prefetch_mmio_base = base;
    bridge->secondary_bus.prefetch_mmio_size = bridge->prefetch_mmio_size;
    bridge->secondary_bus.prefetch_mmio_next = base;
    return 0;
}

static int pci_bridge_config_write(VartPciDevice *device, unsigned int offset,
                                   unsigned int size, uint64_t value)
{
    VartPciBridge *bridge = (VartPciBridge *)device;
    VartPciBus *child = &bridge->secondary_bus;
    VartPciBus *parent = child->parent_pci_bus;
    VartPciBus *root;
    VartPciBus *existing;
    uint8_t secondary;
    uint8_t subordinate;
    uint8_t latency_timer;
    unsigned int i;

    if (offset < 0x18 || offset > 0x1b || size > 4 - (offset - 0x18)) {
        if (offset >= 0x20 && offset <= 0x23 &&
            size <= 4 - (offset - 0x20)) {
            uint32_t config = (uint32_t)config_load_le(
                &device->config_space[0x20], 4);

            for (i = 0; i < size; i++) {
                config &= ~(UINT32_C(0xff) << ((offset - 0x20 + i) * 8));
                config |= ((uint32_t)(value >> (i * 8)) & UINT32_C(0xff)) <<
                          ((offset - 0x20 + i) * 8);
            }
            config_store_le(&device->config_space[0x20], 4, config);
            return pci_bridge_update_mmio_window(bridge, config);
        }
        if (offset >= 0x24 && offset <= 0x2f && size <= 0x30 - offset) {
            config_store_le(&device->config_space[offset], size, value);
            return pci_bridge_update_prefetch_window(bridge);
        }
        return -ENOENT;
    }
    if (parent == NULL) {
        return -EINVAL;
    }
    secondary = child->secondary_number;
    subordinate = child->subordinate_number;
    latency_timer = device->config_space[0x1b];
    for (i = 0; i < size; i++) {
        unsigned int byte_offset = offset + i;
        uint8_t byte = value >> (i * 8);

        if (byte_offset == 0x19) {
            secondary = byte;
        } else if (byte_offset == 0x1a) {
            subordinate = byte;
        } else if (byte_offset == 0x1b) {
            latency_timer = byte;
        }
    }
    /* Linux temporarily disables a bridge while assigning its bus numbers. */
    if (secondary == 0 && subordinate == 0) {
        return 0;
    }
    if (secondary <= parent->number || subordinate < secondary) {
        return -EINVAL;
    }
    root = pci_bus_root(parent);
    existing = vart_pci_bus_find(root, secondary);
    if (existing != NULL && existing != child) {
        return -EEXIST;
    }
    child->number = secondary;
    child->secondary_number = secondary;
    child->subordinate_number = subordinate;
    device->config_space[0x1b] = latency_timer;
    pci_bus_update_requester_ids(child);
    pci_bus_update_ancestors(child);
    return 0;
}

static const VartPciConfigOps pci_bridge_config_ops = {
    .read = pci_bridge_config_read,
    .write = pci_bridge_config_write,
};

static VartPciDevice *pci_ecam_lookup(VartPciRootComplex *complex,
                                       uint64_t offset, unsigned int size,
                                       unsigned int *config_offset)
{
    VartPciBus *bus;
    unsigned int bus_number = offset >> 20;
    unsigned int devfn = (offset >> 12) & 0xff;
    unsigned int register_offset = offset & 0xfff;

    if (register_offset > VART_PCI_CONFIG_SPACE_SIZE - size) {
        return NULL;
    }
    *config_offset = register_offset;
    bus = vart_pci_bus_find(&complex->root_bus, bus_number);
    if (bus == NULL || !pci_bus_visible(&complex->root_bus, bus) ||
        !bus->functions[devfn] ||
        !bus->functions[devfn]->parent.realized) {
        return NULL;
    }
    return bus->functions[devfn];
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
    uint64_t old_prefetch_mmio_next;
    unsigned int i;
    int ret;

    if (bus == NULL || device->bus != &bus->bus) {
        return -EINVAL;
    }
    old_mmio_next = bus->mmio_next;
    old_prefetch_mmio_next = bus->prefetch_mmio_next;
    for (i = 0; i < VART_PCI_BAR_COUNT; i++) {
        VartPciBar *bar = &pci_device->bars[i];
        uint64_t *next;
        uint64_t base;
        uint64_t size;
        uint64_t address;

        if (!bar->implemented || bar->high_part) {
            continue;
        }
        if ((bar->flags & VART_PCI_BAR_PREFETCHABLE) != 0) {
            next = &bus->prefetch_mmio_next;
            base = bus->prefetch_mmio_base;
            size = bus->prefetch_mmio_size;
        } else {
            next = &bus->mmio_next;
            base = bus->mmio_base;
            size = bus->mmio_size;
        }
        address = (*next + bar->size - 1) & ~(bar->size - 1);
        if (address < *next ||
            address > UINT64_MAX - (bar->size - 1)) {
            ret = -EOVERFLOW;
            goto fail;
        }
        if (size == 0 || size < bar->size || base > UINT64_MAX - size ||
            address < base || address > base + size - bar->size) {
            ret = -ERANGE;
            goto fail;
        }
        ret = pci_bar_map(pci_device, i, address);
        if (ret < 0) {
            goto fail;
        }
        *next = address + bar->size;
        pci_bar_update_config(pci_device, i);
    }
    ret = pci_msix_map(pci_device);
    if (ret < 0) {
        goto fail;
    }
    if (!pci_device->memory_enabled) {
        pci_device_disable_memory(pci_device);
    }
    return 0;

fail:
    pci_msix_unmap(pci_device);
    for (i = 0; i < VART_PCI_BAR_COUNT; i++) {
        if (pci_device->bars[i].mapped) {
            pci_bar_unmap(pci_device, i);
        }
    }
    bus->mmio_next = old_mmio_next;
    bus->prefetch_mmio_next = old_prefetch_mmio_next;
    return ret;
}

static void pci_device_unrealize(VartDevice *device)
{
    VartPciDevice *pci_device = device->opaque;
    unsigned int i;

    if (pci_device->intx_irq != NULL && pci_device->intx_asserted) {
        (void)vart_irq_lower(pci_device->intx_irq);
        pci_device->intx_asserted = false;
    }
    pci_msix_unmap(pci_device);
    for (i = 0; i < VART_PCI_BAR_COUNT; i++) {
        pci_bar_unmap(pci_device, i);
    }
}

static void pci_device_reset_internal(VartPciDevice *pci_device)
{
    VartPciMsix *msix = &pci_device->msix;
    unsigned int i;

    pci_device_disable_memory(pci_device);
    if (pci_device->intx_irq != NULL && pci_device->intx_asserted) {
        (void)vart_irq_lower(pci_device->intx_irq);
        pci_device->intx_asserted = false;
    }
    pci_device->memory_enabled = false;
    pci_device->bus_master_enabled = false;
    pci_device->config_space[0x04] = 0;
    pci_device->config_space[0x05] = 0;
    for (i = 0; i < VART_PCI_BAR_COUNT; i++) {
        pci_device->bars[i].probe = false;
    }
    if (msix->configured) {
        msix->enabled = false;
        msix->function_mask = false;
        memset(msix->table, 0, (size_t)msix->vector_count * 16);
        memset(msix->pba, 0, (msix->vector_count + 63) / 64 * 8);
        config_store_le(&pci_device->config_space[
                            msix->capability_offset + 2],
                        2, 0);
    }
}

static void pci_device_reset(VartDevice *device)
{
    pci_device_reset_internal(device->opaque);
}

static const VartDeviceOps pci_device_ops = {
    .realize = pci_device_realize,
    .unrealize = pci_device_unrealize,
    .reset = pci_device_reset,
};

int vart_pci_bus_init(VartPciBus *bus, const char *name,
                      VartBus *parent_bus, VartPciBus *parent_pci_bus,
                      VartAddressSpace *address_space, uint8_t number)
{
    if (bus == NULL || name == NULL || parent_bus == NULL ||
        address_space == NULL || (parent_pci_bus != NULL &&
                                  parent_bus != &parent_pci_bus->bus)) {
        return -EINVAL;
    }
    if (parent_pci_bus != NULL &&
        (number <= parent_pci_bus->number ||
         vart_pci_bus_find(parent_pci_bus, number) != NULL)) {
        return -EINVAL;
    }
    memset(bus, 0, sizeof(*bus));
    vart_bus_init(&bus->bus, name, parent_bus, address_space);
    bus->parent_pci_bus = parent_pci_bus;
    bus->number = number;
    bus->secondary_number = number;
    bus->subordinate_number = UINT8_MAX;
    if (parent_pci_bus != NULL) {
        if (parent_pci_bus->child_count == parent_pci_bus->child_capacity) {
            size_t capacity = parent_pci_bus->child_capacity == 0 ? 4 :
                              parent_pci_bus->child_capacity * 2;
            VartPciBus **children;

            if (capacity < parent_pci_bus->child_capacity ||
                capacity > SIZE_MAX / sizeof(*children)) {
                return -EOVERFLOW;
            }
            children = realloc(parent_pci_bus->children,
                               capacity * sizeof(*children));
            if (children == NULL) {
                return -ENOMEM;
            }
            parent_pci_bus->children = children;
            parent_pci_bus->child_capacity = capacity;
        }
        parent_pci_bus->children[parent_pci_bus->child_count++] = bus;
    }
    return 0;
}

VartPciBus *vart_pci_bus_find(VartPciBus *bus, uint8_t number)
{
    size_t i;

    if (bus == NULL) {
        return NULL;
    }
    if (bus->number == number) {
        return bus;
    }
    for (i = 0; i < bus->child_count; i++) {
        VartPciBus *found = vart_pci_bus_find(bus->children[i], number);

        if (found != NULL) {
            return found;
        }
    }
    return NULL;
}

int vart_pci_bus_destroy(VartPciBus *bus)
{
    VartPciBus *parent;
    size_t i;
    int ret;

    if (bus == NULL) {
        return -EINVAL;
    }
    if (bus->bus.device_count != 0 || bus->child_count != 0) {
        return -EBUSY;
    }
    parent = bus->parent_pci_bus;
    if (parent != NULL) {
        for (i = 0; i < parent->child_count; i++) {
            if (parent->children[i] == bus) {
                memmove(&parent->children[i], &parent->children[i + 1],
                        (parent->child_count - i - 1) *
                        sizeof(*parent->children));
                parent->child_count--;
                break;
            }
        }
    }
    free(bus->children);
    bus->children = NULL;
    bus->child_count = 0;
    bus->child_capacity = 0;
    bus->parent_pci_bus = NULL;
    ret = vart_bus_destroy(&bus->bus);
    return ret;
}

static int pci_bridge_init_with_window(VartPciBridge *bridge,
                                       const char *name,
                                       VartPciBus *parent_bus,
                                       VartAddressSpace *address_space,
                                       uint8_t secondary_number,
                                       uint64_t window_size)
{
    uint64_t old_mmio_next;
    uint64_t window_base;
    uint64_t window_end;
    int ret;

    if (bridge == NULL || name == NULL || parent_bus == NULL ||
        address_space == NULL || secondary_number <= parent_bus->number ||
        window_size == 0 || (window_size & (window_size - 1)) != 0) {
        return -EINVAL;
    }
    if (vart_pci_bus_find(pci_bus_root(parent_bus), secondary_number) != NULL) {
        return -EEXIST;
    }
    if (parent_bus->mmio_size < window_size ||
        parent_bus->mmio_base > UINT64_MAX - parent_bus->mmio_size) {
        return -ENOSPC;
    }
    old_mmio_next = parent_bus->mmio_next;
    if (old_mmio_next < parent_bus->mmio_base) {
        old_mmio_next = parent_bus->mmio_base;
    }
    window_base = (old_mmio_next + window_size - 1) & ~(window_size - 1);
    if (window_base < old_mmio_next ||
        window_base > UINT64_MAX - window_size) {
        return -ENOSPC;
    }
    window_end = window_base + window_size;
    if (window_base < parent_bus->mmio_base ||
        window_end > parent_bus->mmio_base + parent_bus->mmio_size) {
        return -ENOSPC;
    }
    ret = vart_pci_device_init(&bridge->parent, name, 0x1b36, 0x0001, 0, 0,
                               0x04, 0x06);
    if (ret < 0) {
        return ret;
    }
    bridge->parent.config_ops = &pci_bridge_config_ops;
    bridge->parent.config_space[0x0e] = 0x01;
    ret = vart_pci_bus_init(&bridge->secondary_bus, "pci-bridge-secondary",
                            &parent_bus->bus, parent_bus, address_space,
                            secondary_number);
    if (ret < 0) {
        return ret;
    }
    parent_bus->mmio_next = window_end;
    bridge->mmio_base = window_base;
    bridge->mmio_size = window_size;
    bridge->secondary_bus.mmio_base = window_base;
    bridge->secondary_bus.mmio_size = window_size;
    bridge->secondary_bus.mmio_next = window_base;
    bridge->secondary_bus.owner_bridge = bridge;
    ret = pci_bridge_update_prefetch_window(bridge);
    if (ret < 0) {
        vart_pci_bus_destroy(&bridge->secondary_bus);
        return ret;
    }
    bridge->secondary_bus.subordinate_number = secondary_number;
    bridge->parent.config_space[0x18] = parent_bus->number;
    bridge->parent.config_space[0x19] = secondary_number;
    bridge->parent.config_space[0x1a] = secondary_number;
    config_store_le(&bridge->parent.config_space[0x20], 2,
                    (uint16_t)(window_base >> 16) & UINT16_C(0xfff0));
    config_store_le(&bridge->parent.config_space[0x22], 2,
                    ((uint16_t)((window_end - 1) >> 16) & UINT16_C(0xfff0)) |
                    UINT16_C(0x000f));
    pci_bus_update_ancestors(&bridge->secondary_bus);
    return 0;
}

int vart_pci_bridge_init(VartPciBridge *bridge, const char *name,
                         VartPciBus *parent_bus,
                         VartAddressSpace *address_space,
                         uint8_t secondary_number)
{
    return pci_bridge_init_with_window(bridge, name, parent_bus,
                                       address_space, secondary_number,
                                       VART_PCI_BRIDGE_MMIO_WINDOW_SIZE);
}

int vart_pci_bridge_destroy(VartPciBridge *bridge)
{
    if (bridge == NULL) {
        return -EINVAL;
    }
    if (bridge->parent.parent.realized || bridge->parent.pci_bus != NULL) {
        return -EBUSY;
    }
    return vart_pci_bus_destroy(&bridge->secondary_bus);
}

int vart_pci_switch_init(VartPciSwitch *sw, const char *name,
                         VartPciBus *parent_bus,
                         VartAddressSpace *address_space,
                         uint8_t secondary_number, size_t port_count)
{
    uint64_t old_mmio_next;
    uint64_t window_size;
    size_t i;
    int ret;

    if (sw == NULL || name == NULL || parent_bus == NULL ||
        address_space == NULL || port_count == 0 ||
        (port_count & (port_count - 1)) != 0 || port_count > 4 ||
        secondary_number > UINT8_MAX - port_count) {
        return -EINVAL;
    }
    if (port_count > UINT64_MAX / VART_PCI_BRIDGE_MMIO_WINDOW_SIZE) {
        return -EOVERFLOW;
    }
    window_size = port_count * VART_PCI_BRIDGE_MMIO_WINDOW_SIZE;
    old_mmio_next = parent_bus->mmio_next;
    memset(sw, 0, sizeof(*sw));
    sw->downstream_ports = calloc(port_count, sizeof(*sw->downstream_ports));
    if (sw->downstream_ports == NULL) {
        return -ENOMEM;
    }
    sw->downstream_count = port_count;
    ret = pci_bridge_init_with_window(&sw->upstream, name, parent_bus,
                                      address_space, secondary_number,
                                      window_size);
    if (ret < 0) {
        free(sw->downstream_ports);
        sw->downstream_ports = NULL;
        sw->downstream_count = 0;
        return ret;
    }
    for (i = 0; i < port_count; i++) {
        const char *port_name = "pci-switch-downstream";

        ret = pci_bridge_init_with_window(
            &sw->downstream_ports[i], port_name, &sw->upstream.secondary_bus,
            address_space, (uint8_t)(secondary_number + i + 1),
            VART_PCI_BRIDGE_MMIO_WINDOW_SIZE);
        if (ret < 0) {
            while (i > 0) {
                i--;
                vart_pci_bridge_destroy(&sw->downstream_ports[i]);
            }
            vart_pci_bridge_destroy(&sw->upstream);
            free(sw->downstream_ports);
            sw->downstream_ports = NULL;
            sw->downstream_count = 0;
            parent_bus->mmio_next = old_mmio_next;
            return ret;
        }
    }
    return 0;
}

int vart_pci_switch_destroy(VartPciSwitch *sw)
{
    size_t i;
    int ret;

    if (sw == NULL || sw->downstream_ports == NULL ||
        sw->downstream_count == 0) {
        return -EINVAL;
    }
    for (i = 0; i < sw->downstream_count; i++) {
        if (sw->downstream_ports[i].parent.parent.realized ||
            sw->downstream_ports[i].parent.pci_bus != NULL) {
            return -EBUSY;
        }
    }
    for (i = sw->downstream_count; i > 0; i--) {
        ret = vart_pci_bridge_destroy(&sw->downstream_ports[i - 1]);
        if (ret < 0) {
            return ret;
        }
    }
    free(sw->downstream_ports);
    sw->downstream_ports = NULL;
    sw->downstream_count = 0;
    return vart_pci_bridge_destroy(&sw->upstream);
}

int vart_pci_root_complex_init(VartPciRootComplex *complex,
                               VartBus *parent_bus,
                               VartAddressSpace *address_space)
{
    int ret;

    if (complex == NULL || parent_bus == NULL || address_space == NULL) {
        return -EINVAL;
    }
    memset(complex, 0, sizeof(*complex));
    vart_device_init(&complex->parent, "pcie-root-complex",
                     &pci_root_complex_ops, complex, NULL);
    ret = vart_pci_bus_init(&complex->root_bus, "pcie.0", parent_bus, NULL,
                            address_space, VART_PCI_ROOT_BUS_NUMBER);
    if (ret < 0) {
        return ret;
    }
    complex->root_bus.mmio_base = VART_VIRT_PCIE_MMIO_BASE;
    complex->root_bus.mmio_size = VART_VIRT_PCIE_MMIO_SIZE;
    complex->root_bus.mmio_next = VART_VIRT_PCIE_MMIO_BASE;
    ret = vart_address_region_init_mmio(&complex->ecam_region,
                                        VART_VIRT_PCIE_ECAM_BASE,
                                        VART_VIRT_PCIE_ECAM_SIZE, 0,
                                        complex, &pci_ecam_ops, complex);
    if (ret < 0) {
        vart_pci_bus_destroy(&complex->root_bus);
        return ret;
    }
    return 0;
}

int vart_pci_root_complex_destroy(VartPciRootComplex *complex)
{
    int ret;

    if (complex == NULL) {
        return -EINVAL;
    }
    if (complex->root_bus.bus.device_count != 0 ||
        complex->root_bus.child_count != 0) {
        return -EBUSY;
    }
    vart_device_unrealize(&complex->parent);
    if (complex->parent.bus != NULL) {
        ret = vart_bus_remove_device(complex->parent.bus, &complex->parent);
        if (ret < 0) {
            return ret;
        }
    }
    return vart_pci_bus_destroy(&complex->root_bus);
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
    device->capability_next = VART_PCI_CAPABILITY_START;
    return 0;
}

int vart_pci_bus_add_device(VartPciBus *bus, VartPciDevice *device,
                            uint8_t devfn)
{
    unsigned int function = devfn & 0x7;
    uint8_t function0_devfn = devfn & UINT8_C(0xf8);
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
    if (function != 0 && bus->functions[function0_devfn] == NULL) {
        return -EINVAL;
    }
    ret = vart_bus_add_device(&bus->bus, &device->parent);
    if (ret < 0) {
        return ret;
    }
    device->pci_bus = bus;
    if (device->parent.dma_as == NULL) {
        device->parent.dma_as = bus->bus.address_space;
    }
    device->devfn = devfn;
    device->requester_id = ((uint16_t)bus->number << 8) | (uint16_t)devfn;
    bus->functions[devfn] = device;
    if (function != 0) {
        bus->functions[function0_devfn]->config_space[0x0e] |=
            VART_PCI_HEADER_TYPE_MULTIFUNCTION;
    }
    return 0;
}

int vart_pci_bus_remove_device(VartPciBus *bus, VartPciDevice *device)
{
    unsigned int function;
    unsigned int i;
    int ret;

    if (bus == NULL || device == NULL || device->pci_bus != bus ||
        bus->functions[device->devfn] != device) {
        return -ENOENT;
    }
    if (device->parent.realized) {
        return -EBUSY;
    }
    function = device->devfn & 0x7;
    if (function == 0) {
        for (i = 1; i < 8; i++) {
            if (bus->functions[(device->devfn & UINT8_C(0xf8)) + i] != NULL) {
                return -EBUSY;
            }
        }
    }
    ret = vart_bus_remove_device(&bus->bus, &device->parent);
    if (ret < 0) {
        return ret;
    }
    bus->functions[device->devfn] = NULL;
    device->pci_bus = NULL;
    device->requester_id = 0;
    if (function != 0 && bus->functions[device->devfn & UINT8_C(0xf8)] !=
                              NULL) {
        for (i = 1; i < 8; i++) {
            if (bus->functions[(device->devfn & UINT8_C(0xf8)) + i] != NULL) {
                return 0;
            }
        }
        bus->functions[device->devfn & UINT8_C(0xf8)]->config_space[0x0e] &=
            (uint8_t)~VART_PCI_HEADER_TYPE_MULTIFUNCTION;
    }
    return 0;
}

uint16_t vart_pci_device_requester_id(const VartPciDevice *device)
{
    return device != NULL ? device->requester_id : 0;
}

int vart_pci_device_reset(VartPciDevice *device)
{
    if (device == NULL || !device->parent.realized) {
        return -EINVAL;
    }
    pci_device_reset_internal(device);
    return 0;
}

int vart_pci_device_connect_intx(VartPciDevice *device, uint8_t pin,
                                 VartIrq *irq)
{
    if (device == NULL || irq == NULL || pin < 1 || pin > 4) {
        return -EINVAL;
    }
    if (device->parent.realized) {
        return -EBUSY;
    }
    device->intx_irq = irq;
    device->intx_pin = pin;
    device->config_space[0x3c] = irq->line <= UINT8_MAX ?
                                  (uint8_t)irq->line : UINT8_MAX;
    device->config_space[0x3d] = pin;
    return 0;
}

int vart_pci_device_set_intx(VartPciDevice *device, bool level)
{
    int ret;

    if (device == NULL || device->intx_irq == NULL) {
        return -ENOTCONN;
    }
    ret = vart_irq_set(device->intx_irq, level);
    if (ret < 0) {
        return ret;
    }
    device->intx_asserted = level;
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

int vart_pci_device_set_dma_as(VartPciDevice *device,
                               VartAddressSpace *address_space)
{
    if (device == NULL) {
        return -EINVAL;
    }
    if (device->parent.realized) {
        return -EBUSY;
    }
    device->parent.dma_as = address_space;
    return 0;
}

int vart_pci_device_dma_read(VartPciDevice *device, uint64_t guest_addr,
                             void *data, size_t size)
{
    if (device == NULL || !device->bus_master_enabled) {
        return -EPERM;
    }
    if (device->parent.dma_as == NULL) {
        return -ENODEV;
    }
    return vart_address_space_read_buffer(device->parent.dma_as, guest_addr,
                                           data, size);
}

int vart_pci_device_dma_write(VartPciDevice *device, uint64_t guest_addr,
                              const void *data, size_t size)
{
    if (device == NULL || !device->bus_master_enabled) {
        return -EPERM;
    }
    if (device->parent.dma_as == NULL) {
        return -ENODEV;
    }
    return vart_address_space_write_buffer(device->parent.dma_as, guest_addr,
                                            data, size);
}

int vart_pci_device_add_capability(VartPciDevice *device, uint8_t id,
                                   const void *data, unsigned int data_size,
                                   uint8_t *offset)
{
    uint16_t capability_size;
    uint16_t allocation_size;
    uint16_t capability_offset;
    uint16_t status;

    if (device == NULL || id == 0 || offset == NULL ||
        (data == NULL && data_size != 0) || data_size > UINT8_MAX - 2) {
        return -EINVAL;
    }
    if (device->parent.realized || device->pci_bus != NULL) {
        return -EBUSY;
    }
    capability_size = (uint16_t)data_size + 2;
    allocation_size = (capability_size + 3) & ~UINT16_C(3);
    capability_offset = device->capability_next;
    if (capability_offset < VART_PCI_CAPABILITY_START ||
        capability_offset >= VART_PCI_CAPABILITY_END ||
        allocation_size > VART_PCI_CAPABILITY_END - capability_offset) {
        return -ENOSPC;
    }

    memset(&device->config_space[capability_offset], 0, allocation_size);
    device->config_space[capability_offset] = id;
    if (data_size != 0) {
        memcpy(&device->config_space[capability_offset + 2], data,
               data_size);
    }
    if (device->capability_tail == 0) {
        device->config_space[VART_PCI_CAPABILITY_LIST_OFFSET] =
            (uint8_t)capability_offset;
    } else {
        device->config_space[device->capability_tail + 1] =
            (uint8_t)capability_offset;
    }
    device->capability_tail = (uint8_t)capability_offset;
    device->capability_next = capability_offset + allocation_size;
    status = (uint16_t)config_load_le(&device->config_space[0x06], 2);
    status |= VART_PCI_STATUS_CAPABILITIES;
    config_store_le(&device->config_space[0x06], 2, status);
    *offset = (uint8_t)capability_offset;
    return 0;
}

int vart_pci_device_init_msix(VartPciDevice *device, unsigned int vector_count,
                              unsigned int table_bar, uint32_t table_offset,
                              unsigned int pba_bar, uint32_t pba_offset)
{
    VartPciMsix *msix;
    uint8_t capability_body[10] = { 0 };
    uint8_t capability_offset;
    uint16_t control;
    uint64_t table_size;
    uint64_t pba_size;
    int ret;

    if (device == NULL || vector_count == 0 ||
        vector_count > VART_PCI_MSIX_MAX_VECTORS ||
        table_bar >= VART_PCI_BAR_COUNT || pba_bar >= VART_PCI_BAR_COUNT ||
        (table_offset & 7) != 0 || (pba_offset & 7) != 0) {
        return -EINVAL;
    }
    if (device->parent.realized || device->pci_bus != NULL) {
        return -EBUSY;
    }
    msix = &device->msix;
    if (msix->configured) {
        return -EALREADY;
    }
    table_size = (uint64_t)vector_count * 16;
    pba_size = ((vector_count + 63) / 64) * 8;
    msix->table = calloc(1, table_size);
    msix->pba = calloc(1, pba_size);
    msix->irq_routes = calloc(vector_count, sizeof(*msix->irq_routes));
    if (msix->table == NULL || msix->pba == NULL ||
        msix->irq_routes == NULL) {
        free(msix->table);
        free(msix->pba);
        free(msix->irq_routes);
        msix->table = NULL;
        msix->pba = NULL;
        msix->irq_routes = NULL;
        return -ENOMEM;
    }
    control = (uint16_t)(vector_count - 1) & UINT16_C(0x07ff);
    config_store_le(&capability_body[0], 2, control);
    config_store_le(&capability_body[2], 4,
                    table_offset | (uint32_t)table_bar);
    config_store_le(&capability_body[6], 4, pba_offset | (uint32_t)pba_bar);
    ret = vart_pci_device_add_capability(device, VART_PCI_CAP_ID_MSIX,
                                         capability_body,
                                         sizeof(capability_body),
                                         &capability_offset);
    if (ret < 0) {
        free(msix->table);
        free(msix->pba);
        free(msix->irq_routes);
        msix->table = NULL;
        msix->pba = NULL;
        msix->irq_routes = NULL;
        return ret;
    }
    msix->table_offset = table_offset;
    msix->pba_offset = pba_offset;
    msix->table_bar = table_bar;
    msix->pba_bar = pba_bar;
    msix->capability_offset = capability_offset;
    msix->vector_count = vector_count;
    msix->configured = true;
    return 0;
}

int vart_pci_device_connect_msix(VartPciDevice *device, unsigned int vector,
                                 VartIrq *irq)
{
    if (device == NULL || !device->msix.configured ||
        vector >= device->msix.vector_count || irq == NULL || irq->set == NULL) {
        return -EINVAL;
    }
    device->msix.irq_routes[vector] = irq;
    return 0;
}

int vart_pci_device_set_msix_injector(VartPciDevice *device,
                                      VartPciMsixInject inject, void *opaque)
{
    if (device == NULL || !device->msix.configured) {
        return -EINVAL;
    }
    device->msix.inject = inject;
    device->msix.inject_opaque = opaque;
    return 0;
}

int vart_pci_device_raise_msix(VartPciDevice *device, unsigned int vector)
{
    VartPciMsix *msix;

    if (device == NULL || !device->msix.configured ||
        vector >= device->msix.vector_count) {
        return -EINVAL;
    }
    msix = &device->msix;
    msix->pba[vector / 8] |= (uint8_t)(1U << (vector % 8));
    if (!msix->enabled || msix->function_mask ||
        ((uint32_t)config_load_le(&msix->table[vector * 16 + 12], 4) & 1)) {
        return 0;
    }
    return pci_msix_deliver(device, vector);
}
