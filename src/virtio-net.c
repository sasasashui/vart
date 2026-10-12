#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "vart/virtio-net.h"

#define VIRTIO_F_VERSION_1 UINT64_C(32)
#define VIRTIO_NET_F_MAC UINT64_C(5)
#define VIRTIO_NET_F_STATUS UINT64_C(16)
#define VIRTIO_NET_S_LINK_UP UINT16_C(1)
#define VIRTIO_NET_HDR_SIZE 12

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
    VartVirtioNetDevice *device = opaque;
    VartVirtioChain chain;
    uint8_t *packet = NULL;
    uint32_t packet_length;
    uint32_t copied;
    bool completed = false;
    ssize_t written;
    int ret;

    (void)pci;
    if (device == NULL || device->backend_fd < 0 || queue_index == 0) {
        return 0;
    }
    if (queue_index != 1) {
        return -EINVAL;
    }
    for (;;) {
        packet_length = 0;
        ret = vart_virtio_queue_pop(&device->pci.virtio, queue_index,
                                    &chain);
        if (ret == -EAGAIN) {
            break;
        }
        if (ret < 0) {
            return ret;
        }
        if (chain.count < 2 ||
            (chain.descriptors[0].flags & VART_VIRTIO_DESC_F_WRITE) ||
            chain.descriptors[0].length < VIRTIO_NET_HDR_SIZE) {
            return -EPROTO;
        }
        for (unsigned int i = 1; i < chain.count; i++) {
            if (chain.descriptors[i].flags & VART_VIRTIO_DESC_F_WRITE) {
                return -EPROTO;
            }
            if (packet_length > UINT32_MAX - chain.descriptors[i].length) {
                return -EOVERFLOW;
            }
            packet_length += chain.descriptors[i].length;
        }
        if (packet_length == 0) {
            return -EPROTO;
        }
        packet = malloc(packet_length);
        if (packet == NULL) {
            return -ENOMEM;
        }
        copied = 0;
        for (unsigned int i = 1; i < chain.count; i++) {
            ret = virtio_net_dma_read(device, chain.descriptors[i].address,
                                      packet + copied,
                                      chain.descriptors[i].length);
            if (ret < 0) {
                free(packet);
                return ret;
            }
            copied += chain.descriptors[i].length;
        }
        do {
            written = write(device->backend_fd, packet, packet_length);
        } while (written < 0 && errno == EINTR);
        free(packet);
        packet = NULL;
        if (written < 0) {
            return -errno;
        }
        if ((uint64_t)written != packet_length) {
            return -EIO;
        }
        ret = vart_virtio_queue_push_used(&device->pci.virtio, queue_index,
                                          chain.head, packet_length);
        if (ret < 0) {
            return ret;
        }
        completed = true;
    }
    return completed ? vart_virtio_pci_raise_queue_interrupt(&device->pci,
                                                               queue_index) : 0;
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
    device->backend_fd = -1;
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

int vart_virtio_net_attach_backend(VartVirtioNetDevice *device, int fd)
{
    if (device == NULL || !device->initialized || fd < 0 ||
        device->backend_fd >= 0) {
        return -EINVAL;
    }
    device->backend_fd = fd;
    device->backend_owned = true;
    return 0;
}

static int virtio_net_tap_ioctl(int fd, struct ifreq *request)
{
    if (ioctl(fd, TUNSETIFF, request) < 0) {
        return -errno;
    }
    return 0;
}

int vart_virtio_net_open_tap(VartVirtioNetDevice *device,
                             const char *interface_name)
{
    struct ifreq request = { 0 };
    int fd;
    int ret;

    if (device == NULL || !device->initialized ||
        device->backend_fd >= 0) {
        return -EINVAL;
    }
    fd = open("/dev/net/tun", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        return -errno;
    }
    request.ifr_flags = IFF_TAP | IFF_NO_PI;
    if (interface_name != NULL) {
        if (strlen(interface_name) >= IFNAMSIZ) {
            close(fd);
            return -ENAMETOOLONG;
        }
        strcpy(request.ifr_name, interface_name);
    }
    ret = virtio_net_tap_ioctl(fd, &request);
    if (ret < 0) {
        close(fd);
        return ret;
    }
    device->backend_fd = fd;
    device->backend_owned = true;
    return 0;
}

int vart_virtio_net_detach_backend(VartVirtioNetDevice *device)
{
    if (device == NULL) {
        return -EINVAL;
    }
    if (device->backend_fd >= 0 && device->backend_owned) {
        close(device->backend_fd);
    }
    device->backend_fd = -1;
    device->backend_owned = false;
    return 0;
}

int vart_virtio_net_destroy(VartVirtioNetDevice *device)
{
    int ret;

    if (device == NULL || !device->initialized) {
        return -EINVAL;
    }
    ret = vart_virtio_net_detach_backend(device);
    if (ret < 0) {
        return ret;
    }
    ret = vart_virtio_pci_device_destroy(&device->pci);
    if (ret < 0) {
        return ret;
    }
    device->initialized = false;
    return 0;
}
