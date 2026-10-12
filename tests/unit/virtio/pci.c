#include <errno.h>
#include <stdint.h>
#include <stdio.h>

#include "vart/address-space.h"
#include "vart/bus.h"
#include "vart/machine/virt.h"
#include "vart/pci.h"
#include "vart/virtio-pci.h"

typedef struct TestContext {
    unsigned int notifications;
    unsigned int last_queue;
} TestContext;

static int dma_read(void *opaque, uint64_t address, void *data, size_t size)
{
    (void)opaque;
    (void)address;
    (void)data;
    (void)size;
    return -ENOSYS;
}

static int dma_write(void *opaque, uint64_t address, const void *data,
                     size_t size)
{
    (void)opaque;
    (void)address;
    (void)data;
    (void)size;
    return -ENOSYS;
}

static int notify_queue(VartVirtioPciDevice *device, unsigned int queue_index,
                        void *opaque)
{
    TestContext *context = opaque;

    (void)device;
    context->notifications++;
    context->last_queue = queue_index;
    return 0;
}

static int check(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "not ok - %s\n", message);
        return -1;
    }
    return 0;
}

static int read_config(const VartAddressSpace *address_space, uint64_t offset,
                       unsigned int size, uint64_t *value)
{
    return vart_address_space_read(address_space,
                                   VART_VIRT_PCIE_ECAM_BASE + offset, size,
                                   value);
}

static int write_config(const VartAddressSpace *address_space, uint64_t offset,
                        unsigned int size, uint64_t value)
{
    return vart_address_space_write(address_space,
                                    VART_VIRT_PCIE_ECAM_BASE + offset, size,
                                    value);
}

int main(void)
{
    const VartVirtioDmaOps dma = {
        .read = dma_read,
        .write = dma_write,
    };
    const VartVirtioPciConfigOps config_ops = { 0 };
    TestContext context = { 0 };
    VartAddressSpace address_space;
    VartBus parent_bus;
    VartPciRootComplex complex;
    VartVirtioPciDevice device;
    uint64_t bar_base;
    uint64_t value;
    uint8_t capability;
    unsigned int type;
    unsigned int i;
    int ret;

    vart_address_space_init(&address_space);
    vart_bus_init(&parent_bus, "test-parent", NULL, &address_space);
    ret = vart_pci_root_complex_init(&complex, &parent_bus, &address_space);
    if (ret < 0 || vart_bus_add_device(&parent_bus, &complex.parent) < 0 ||
        vart_device_realize(&complex.parent) < 0) {
        return 1;
    }
    ret = vart_virtio_pci_device_init(&device, 2, UINT64_C(0x5), 1, &dma,
                                      &config_ops, notify_queue, &context);
    if (ret < 0 || vart_pci_bus_add_device(&complex.root_bus, &device.pci, 0) <
                      0 || vart_device_realize(&device.pci.parent) < 0) {
        return 1;
    }
    bar_base = device.pci.bars[0].address;

    if (check(read_config(&address_space, 0, 4, &value) == 0 &&
              value == UINT64_C(0x10421af4), "virtio PCI identity") < 0 ||
        check(read_config(&address_space, 0x34, 1, &value) == 0 &&
              (capability = (uint8_t)value) == 0x40,
              "virtio capability list") < 0) {
        return 1;
    }
    for (i = 0; i < 4; i++) {
        if (read_config(&address_space, capability, 1, &value) < 0 ||
            value != 0x09 || read_config(&address_space, capability + 3, 1,
                                         &value) < 0) {
            return 1;
        }
        type = (unsigned int)value;
        if (check(type == i + 1, "virtio vendor capability type") < 0 ||
            read_config(&address_space, capability + 1, 1, &value) < 0) {
            return 1;
        }
        capability = (uint8_t)value;
    }
    if (check(capability == 0, "virtio capability list terminator") < 0 ||
        check(write_config(&address_space, 4, 2, 0x6) == 0,
              "enable PCI memory and bus mastering") < 0 ||
        check(vart_address_space_read(&address_space, bar_base + 4, 4,
                                      &value) == 0 && value == 0x5,
              "read device features") < 0 ||
        check(vart_address_space_write(&address_space, bar_base + 0x0c, 4,
                                       0x5) == 0,
              "write driver features") < 0 ||
        check(vart_address_space_read(&address_space, bar_base + 0x12, 2,
                                      &value) == 0 && value == 1,
              "read queue count") < 0 ||
        check(vart_address_space_write(&address_space, bar_base + 0x16, 2,
                                       0) == 0 &&
              vart_address_space_write(&address_space, bar_base + 0x18, 2,
                                       4) == 0,
              "select and size queue") < 0 ||
        check(vart_address_space_write(&address_space, bar_base + 0x20, 4,
                                       0x100) == 0 &&
              vart_address_space_write(&address_space, bar_base + 0x28, 4,
                                       0x1000) == 0 &&
              vart_address_space_write(&address_space, bar_base + 0x30, 4,
                                       0x1100) == 0,
              "write split queue addresses") < 0 ||
        check(vart_address_space_write(&address_space, bar_base + 0x1c, 2,
                                       1) == 0 &&
              device.virtio.queues[0].ready,
              "enable split queue") < 0 ||
        check(vart_address_space_write(&address_space, bar_base + 0x14, 1,
                                       VART_VIRTIO_STATUS_ACKNOWLEDGE |
                                       VART_VIRTIO_STATUS_DRIVER |
                                       VART_VIRTIO_STATUS_DRIVER_OK) == 0,
              "set virtio status") < 0 ||
        check(vart_address_space_write(&address_space, bar_base + 0x100, 2,
                                       0) == 0 && context.notifications == 1 &&
              context.last_queue == 0,
              "notify virtqueue") < 0 ||
        check(vart_virtio_pci_raise_interrupt(&device, false) == 0 &&
              vart_address_space_read(&address_space, bar_base + 0x200, 1,
                                      &value) == 0 && value == 1,
              "read and clear queue interrupt") < 0 ||
        check(vart_address_space_read(&address_space, bar_base + 0x200, 1,
                                      &value) == 0 && value == 0,
              "clear ISR state") < 0 ||
        check(vart_address_space_write(&address_space, bar_base + 0x1c, 2,
                                       0) == 0 &&
              vart_address_space_read(&address_space, bar_base + 0x18, 2,
                                      &value) == 0 && value == 1024 &&
              device.queue_state[0].descriptor_address == 0,
              "reset virtqueue transport state") < 0 ||
        check(vart_address_space_write(&address_space, bar_base + 0x14, 1,
                                       0) == 0 &&
              vart_address_space_read(&address_space, bar_base + 0x18, 2,
                                      &value) == 0 && value == 1024,
              "reset device transport state") < 0) {
        return 1;
    }

    vart_device_unrealize(&device.pci.parent);
    if (vart_pci_bus_remove_device(&complex.root_bus, &device.pci) < 0 ||
        vart_virtio_pci_device_destroy(&device) < 0 ||
        vart_pci_root_complex_destroy(&complex) < 0) {
        return 1;
    }
    vart_bus_destroy(&parent_bus);
    vart_address_space_destroy(&address_space);
    puts("ok - virtio-pci modern transport");
    return 0;
}
