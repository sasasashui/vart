#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "vart/virtio-net.h"

#define VIRTIO_F_VERSION_1 UINT64_C(32)
#define VIRTIO_NET_F_MAC UINT64_C(5)
#define VIRTIO_NET_F_STATUS UINT64_C(16)
#define VIRTIO_NET_S_LINK_UP UINT16_C(1)

static int virtio_net_dma_read(void *opaque, uint64_t address, void *data,
                               size_t size)
{
    VartVirtioNetDevice *device = opaque;

    return vart_pci_device_dma_read(&device->pci.pci, address, data, size);
}

static int virtio_net_dma_write(void *opaque, uint64_t address,
                                const void *data, size_t size)
{
    VartVirtioNetDevice *device = opaque;

    return vart_pci_device_dma_write(&device->pci.pci, address, data, size);
}

static int virtio_net_config_read(VartVirtioPciDevice *pci, uint64_t offset,
                                  unsigned int size, uint64_t *value,
                                  void *opaque)
{
    VartVirtioNetDevice *device = opaque;
    unsigned int i;

    (void)pci;
    if (value == NULL || offset > sizeof(device->config) ||
        size > sizeof(device->config) - offset) {
        return -EINVAL;
    }
    *value = 0;
    for (i = 0; i < size; i++) {
        *value |= (uint64_t)device->config[offset + i] << (i * 8);
    }
    return 0;
}

static int virtio_net_config_write(VartVirtioPciDevice *pci, uint64_t offset,
                                   unsigned int size, uint64_t value,
                                   void *opaque)
{
    (void)pci;
    (void)offset;
    (void)size;
    (void)value;
    (void)opaque;
    return -EPERM;
}

static int virtio_net_notify(VartVirtioPciDevice *pci, unsigned int queue_index,
                             void *opaque)
{
    (void)pci;
    (void)queue_index;
    (void)opaque;
    return 0;
}

int vart_virtio_net_init(VartVirtioNetDevice *device, const uint8_t mac[6],
                         VartAddressSpace *dma_as)
{
    const VartVirtioDmaOps dma = {
        .read = virtio_net_dma_read,
        .write = virtio_net_dma_write,
        .opaque = device,
    };
    const VartVirtioPciConfigOps config_ops = {
        .read = virtio_net_config_read,
        .write = virtio_net_config_write,
        .opaque = device,
    };
    uint64_t features;
    int ret;

    if (device == NULL || mac == NULL || dma_as == NULL) {
        return -EINVAL;
    }
    memset(device, 0, sizeof(*device));
    memcpy(device->config, mac, 6);
    device->config[6] = (uint8_t)VIRTIO_NET_S_LINK_UP;
    device->config[7] = (uint8_t)(VIRTIO_NET_S_LINK_UP >> 8);
    device->config[8] = 1;
    features = (UINT64_C(1) << VIRTIO_F_VERSION_1) |
               (UINT64_C(1) << VIRTIO_NET_F_MAC) |
               (UINT64_C(1) << VIRTIO_NET_F_STATUS);
    ret = vart_virtio_pci_device_init(&device->pci, 1, features, 2, &dma,
                                      &config_ops, virtio_net_notify, device);
    if (ret < 0) {
        return ret;
    }
    ret = vart_virtio_pci_device_set_class_code(&device->pci, 0x02);
    if (ret < 0) {
        goto fail_pci;
    }
    ret = vart_pci_device_init_msix(&device->pci.pci, 3, 0, 0x800, 0,
                                    0xc00);
    if (ret < 0) {
        goto fail_pci;
    }
    ret = vart_pci_device_set_dma_as(&device->pci.pci, dma_as);
    if (ret < 0) {
        goto fail_pci;
    }
    device->initialized = true;
    return 0;

fail_pci:
    vart_virtio_pci_device_destroy(&device->pci);
    return ret;
}

int vart_virtio_net_destroy(VartVirtioNetDevice *device)
{
    int ret;

    if (device == NULL || !device->initialized) {
        return -EINVAL;
    }
    ret = vart_virtio_pci_device_destroy(&device->pci);
    if (ret < 0) {
        return ret;
    }
    device->initialized = false;
    return 0;
}
