#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "vart/virtio-pci.h"

#define VIRTIO_PCI_CAP_VENDOR UINT8_C(0x09)
#define VIRTIO_PCI_CAP_COMMON_CFG UINT8_C(1)
#define VIRTIO_PCI_CAP_NOTIFY_CFG UINT8_C(2)
#define VIRTIO_PCI_CAP_ISR_CFG UINT8_C(3)
#define VIRTIO_PCI_CAP_DEVICE_CFG UINT8_C(4)
#define VIRTIO_PCI_CAP_HEADER_SIZE 16
#define VIRTIO_PCI_NOTIFY_CAP_SIZE 20

static void virtio_pci_store_le32(uint8_t *data, uint32_t value)
{
    data[0] = value;
    data[1] = value >> 8;
    data[2] = value >> 16;
    data[3] = value >> 24;
}

static uint16_t virtio_pci_load16(uint64_t value)
{
    return (uint16_t)value;
}

static uint32_t virtio_pci_load32(uint64_t value)
{
    return (uint32_t)value;
}

static void virtio_pci_reset_queue_state(VartVirtioPciDevice *device,
                                         unsigned int queue_index)
{
    VartVirtioPciQueueState *queue = &device->queue_state[queue_index];

    queue->size = 0;
    queue->msix_vector = VART_VIRTIO_PCI_NO_VECTOR;
    queue->descriptor_address = 0;
    queue->available_address = 0;
    queue->used_address = 0;
}

static int virtio_pci_read_common(VartVirtioPciDevice *device,
                                  uint64_t offset, unsigned int size,
                                  uint64_t *value)
{
    VartVirtioPciQueueState *queue;

    *value = 0;
    if (size == 4 && offset == 0x00) {
        *value = device->device_feature_select;
    } else if (size == 4 && offset == 0x04) {
        *value = device->device_feature_select == 0 ?
                 (uint32_t)device->virtio.device_features :
                 (uint32_t)(device->virtio.device_features >> 32);
    } else if (size == 4 && offset == 0x08) {
        *value = device->driver_feature_select;
    } else if (size == 4 && offset == 0x0c) {
        *value = device->driver_feature_select == 0 ?
                 (uint32_t)device->virtio.driver_features :
                 (uint32_t)(device->virtio.driver_features >> 32);
    } else if (size == 2 && offset == 0x10) {
        *value = device->config_msix_vector;
    } else if (size == 2 && offset == 0x12) {
        *value = device->virtio.queue_count;
    } else if (size == 1 && offset == 0x14) {
        *value = device->virtio.status;
    } else if (size == 1 && offset == 0x15) {
        *value = device->config_generation;
    } else if (size == 2 && offset == 0x16) {
        *value = device->queue_select;
    } else if (size == 2 && offset == 0x18) {
        if (device->queue_select >= device->virtio.queue_count) {
            return -EINVAL;
        }
        queue = &device->queue_state[device->queue_select];
        *value = queue->size != 0 ? queue->size : VART_VIRTIO_MAX_QUEUE_SIZE;
    } else if (size == 2 && offset == 0x1a) {
        if (device->queue_select >= device->virtio.queue_count) {
            return -EINVAL;
        }
        *value = device->queue_state[device->queue_select].msix_vector;
    } else if (size == 2 && offset == 0x1c) {
        if (device->queue_select >= device->virtio.queue_count) {
            return -EINVAL;
        }
        *value = device->virtio.queues[device->queue_select].ready;
    } else if (size == 2 && offset == 0x1e) {
        *value = device->queue_select;
    } else if (size == 4 && (offset == 0x20 || offset == 0x24 ||
                             offset == 0x28 || offset == 0x2c ||
                             offset == 0x30 || offset == 0x34)) {
        if (device->queue_select >= device->virtio.queue_count) {
            return -EINVAL;
        }
        queue = &device->queue_state[device->queue_select];
        if (offset == 0x20 || offset == 0x24) {
            *value = (uint32_t)(queue->descriptor_address >>
                                (offset == 0x20 ? 0 : 32));
        } else if (offset == 0x28 || offset == 0x2c) {
            *value = (uint32_t)(queue->available_address >>
                                (offset == 0x28 ? 0 : 32));
        } else {
            *value = (uint32_t)(queue->used_address >>
                                (offset == 0x30 ? 0 : 32));
        }
    } else {
        return -EINVAL;
    }
    return 0;
}

static int virtio_pci_write_common(VartVirtioPciDevice *device,
                                   uint64_t offset, unsigned int size,
                                   uint64_t value)
{
    VartVirtioPciQueueState *queue;
    uint64_t driver_features;
    int ret;

    if (size == 4 && offset == 0x00) {
        device->device_feature_select = virtio_pci_load32(value);
    } else if (size == 4 && offset == 0x08) {
        device->driver_feature_select = virtio_pci_load32(value);
    } else if (size == 4 && offset == 0x0c) {
        driver_features = device->virtio.driver_features;
        if (device->driver_feature_select == 0) {
            driver_features = (driver_features & UINT64_C(0xffffffff00000000)) |
                              virtio_pci_load32(value);
        } else {
            driver_features = (driver_features & UINT64_C(0xffffffff)) |
                              (uint64_t)virtio_pci_load32(value) << 32;
        }
        return vart_virtio_device_set_features(&device->virtio,
                                               driver_features);
    } else if (size == 2 && offset == 0x10) {
        device->config_msix_vector = virtio_pci_load16(value);
    } else if (size == 2 && offset == 0x1a) {
        if (device->queue_select >= device->virtio.queue_count) {
            return -EINVAL;
        }
        device->queue_state[device->queue_select].msix_vector =
            virtio_pci_load16(value);
    } else if (size == 1 && offset == 0x14) {
        ret = vart_virtio_device_set_status(&device->virtio,
                                            (uint8_t)value);
        if (ret < 0) {
            return ret;
        }
        if (value == 0) {
            for (unsigned int i = 0; i < device->virtio.queue_count; i++) {
                virtio_pci_reset_queue_state(device, i);
            }
        }
        return 0;
    } else if (size == 2 && offset == 0x16) {
        if (value >= device->virtio.queue_count) {
            return -EINVAL;
        }
        device->queue_select = virtio_pci_load16(value);
    } else if (size == 2 && offset == 0x18) {
        if (device->queue_select >= device->virtio.queue_count ||
            value == 0 || value > VART_VIRTIO_MAX_QUEUE_SIZE ||
            (value & (value - 1)) != 0) {
            return -EINVAL;
        }
        device->queue_state[device->queue_select].size =
            virtio_pci_load16(value);
    } else if (size == 2 && offset == 0x1c) {
        if (device->queue_select >= device->virtio.queue_count) {
            return -EINVAL;
        }
        queue = &device->queue_state[device->queue_select];
        if (value == 0) {
            ret = vart_virtio_queue_reset(&device->virtio,
                                          device->queue_select);
            if (ret < 0) {
                return ret;
            }
            virtio_pci_reset_queue_state(device, device->queue_select);
            return 0;
        }
        if (value != 1 || queue->size == 0) {
            return -EINVAL;
        }
        ret = vart_virtio_queue_configure(
            &device->virtio, device->queue_select, queue->size,
            queue->descriptor_address, queue->available_address,
            queue->used_address);
        return ret;
    } else if (size == 4 && (offset == 0x20 || offset == 0x24 ||
                             offset == 0x28 || offset == 0x2c ||
                             offset == 0x30 || offset == 0x34)) {
        if (device->queue_select >= device->virtio.queue_count) {
            return -EINVAL;
        }
        queue = &device->queue_state[device->queue_select];
        if (offset == 0x20 || offset == 0x24) {
            if (offset == 0x20) {
                queue->descriptor_address =
                    (queue->descriptor_address & UINT64_C(0xffffffff00000000)) |
                    virtio_pci_load32(value);
            } else {
                queue->descriptor_address =
                    (queue->descriptor_address & UINT64_C(0xffffffff)) |
                    (uint64_t)virtio_pci_load32(value) << 32;
            }
        } else if (offset == 0x28 || offset == 0x2c) {
            if (offset == 0x28) {
                queue->available_address =
                    (queue->available_address & UINT64_C(0xffffffff00000000)) |
                    virtio_pci_load32(value);
            } else {
                queue->available_address =
                    (queue->available_address & UINT64_C(0xffffffff)) |
                    (uint64_t)virtio_pci_load32(value) << 32;
            }
        } else {
            if (offset == 0x30) {
                queue->used_address =
                    (queue->used_address & UINT64_C(0xffffffff00000000)) |
                    virtio_pci_load32(value);
            } else {
                queue->used_address =
                    (queue->used_address & UINT64_C(0xffffffff)) |
                    (uint64_t)virtio_pci_load32(value) << 32;
            }
        }
    } else {
        return -EINVAL;
    }
    return 0;
}

static int virtio_pci_bar_read(void *opaque, uint64_t offset,
                               unsigned int size, uint64_t *value)
{
    VartVirtioPciDevice *device = opaque;
    uint64_t relative;
    int ret;

    if (offset < VART_VIRTIO_PCI_COMMON_OFFSET +
                 VART_VIRTIO_PCI_COMMON_SIZE) {
        return virtio_pci_read_common(device,
                                      offset - VART_VIRTIO_PCI_COMMON_OFFSET,
                                      size, value);
    }
    if (offset >= VART_VIRTIO_PCI_ISR_OFFSET && size == 1 &&
        offset < VART_VIRTIO_PCI_ISR_OFFSET + 1) {
        *value = device->isr_status;
        device->isr_status = 0;
        return 0;
    }
    if (offset >= VART_VIRTIO_PCI_DEVICE_OFFSET &&
        offset < VART_VIRTIO_PCI_DEVICE_OFFSET + VART_VIRTIO_PCI_DEVICE_SIZE) {
        relative = offset - VART_VIRTIO_PCI_DEVICE_OFFSET;
        if (relative + size > VART_VIRTIO_PCI_DEVICE_SIZE) {
            return -EINVAL;
        }
        if (device->config_ops.read == NULL) {
            *value = 0;
            return 0;
        }
        ret = device->config_ops.read(device, relative, size, value,
                                      device->config_ops.opaque);
        return ret;
    }
    return -EINVAL;
}

static int virtio_pci_bar_write(void *opaque, uint64_t offset,
                                unsigned int size, uint64_t value)
{
    VartVirtioPciDevice *device = opaque;
    uint64_t relative;
    unsigned int queue_index;
    int ret;

    if (offset < VART_VIRTIO_PCI_COMMON_OFFSET +
                 VART_VIRTIO_PCI_COMMON_SIZE) {
        return virtio_pci_write_common(device,
                                       offset - VART_VIRTIO_PCI_COMMON_OFFSET,
                                       size, value);
    }
    if (offset >= VART_VIRTIO_PCI_NOTIFY_OFFSET &&
        offset < VART_VIRTIO_PCI_NOTIFY_OFFSET +
                 device->virtio.queue_count * VART_VIRTIO_PCI_NOTIFY_MULTIPLIER) {
        if (size != 2 && size != 4) {
            return -EINVAL;
        }
        queue_index = (offset - VART_VIRTIO_PCI_NOTIFY_OFFSET) /
                      VART_VIRTIO_PCI_NOTIFY_MULTIPLIER;
        if ((offset - VART_VIRTIO_PCI_NOTIFY_OFFSET) %
                VART_VIRTIO_PCI_NOTIFY_MULTIPLIER != 0 ||
            device->notify == NULL) {
            return 0;
        }
        return device->notify(device, queue_index, device->notify_opaque);
    }
    if (offset >= VART_VIRTIO_PCI_DEVICE_OFFSET &&
        offset < VART_VIRTIO_PCI_DEVICE_OFFSET + VART_VIRTIO_PCI_DEVICE_SIZE) {
        relative = offset - VART_VIRTIO_PCI_DEVICE_OFFSET;
        if (relative + size > VART_VIRTIO_PCI_DEVICE_SIZE) {
            return -EINVAL;
        }
        if (device->config_ops.write == NULL) {
            return -ENOSYS;
        }
        ret = device->config_ops.write(device, relative, size, value,
                                       device->config_ops.opaque);
        return ret;
    }
    return -EINVAL;
}

static const VartMmioOps virtio_pci_bar_ops = {
    .read = virtio_pci_bar_read,
    .write = virtio_pci_bar_write,
};

static int virtio_pci_add_capability(VartVirtioPciDevice *device,
                                     uint8_t type, uint32_t offset,
                                     uint32_t length, uint32_t multiplier)
{
    uint8_t body[VIRTIO_PCI_NOTIFY_CAP_SIZE - 2] = { 0 };
    unsigned int body_size;
    uint8_t capability_offset;

    body_size = multiplier == 0 ? VIRTIO_PCI_CAP_HEADER_SIZE - 2 :
                VIRTIO_PCI_NOTIFY_CAP_SIZE - 2;
    body[0] = (uint8_t)(body_size + 2);
    body[1] = type;
    body[2] = 0;
    body[6] = 0;
    virtio_pci_store_le32(&body[6], offset);
    virtio_pci_store_le32(&body[10], length);
    if (multiplier != 0) {
        virtio_pci_store_le32(&body[14], multiplier);
    }
    return vart_pci_device_add_capability(&device->pci, VIRTIO_PCI_CAP_VENDOR,
                                          body, body_size,
                                          &capability_offset);
}

int vart_virtio_pci_device_init(VartVirtioPciDevice *device,
                                uint32_t device_id, uint64_t device_features,
                                unsigned int queue_count,
                                const VartVirtioDmaOps *dma,
                                const VartVirtioPciConfigOps *config_ops,
                                VartVirtioPciNotify notify,
                                void *notify_opaque)
{
    uint16_t pci_device_id;
    int ret;

    if (device == NULL || device_id > UINT16_MAX - 0x1040 ||
        queue_count == 0) {
        return -EINVAL;
    }
    memset(device, 0, sizeof(*device));
    ret = vart_virtio_device_init(&device->virtio, device_id,
                                  device_features, queue_count, dma);
    if (ret < 0) {
        return ret;
    }
    device->queue_state = calloc(queue_count, sizeof(*device->queue_state));
    if (device->queue_state == NULL) {
        vart_virtio_device_destroy(&device->virtio);
        return -ENOMEM;
    }
    for (unsigned int i = 0; i < queue_count; i++) {
        device->queue_state[i].msix_vector = VART_VIRTIO_PCI_NO_VECTOR;
    }
    device->config_msix_vector = VART_VIRTIO_PCI_NO_VECTOR;
    device->config_generation = 1;
    device->notify = notify;
    device->notify_opaque = notify_opaque;
    if (config_ops != NULL) {
        device->config_ops = *config_ops;
    }
    pci_device_id = (uint16_t)(0x1040 + device_id);
    ret = vart_pci_device_init(&device->pci, "virtio-pci",
                               VART_VIRTIO_PCI_VENDOR_ID, pci_device_id,
                               0, 0, 0, 0x02);
    if (ret < 0) {
        goto fail;
    }
    ret = vart_pci_device_set_bar(&device->pci, 0, VART_VIRTIO_PCI_BAR_SIZE,
                                  0, &virtio_pci_bar_ops, device);
    if (ret < 0) {
        goto fail;
    }
    ret = virtio_pci_add_capability(device, VIRTIO_PCI_CAP_COMMON_CFG, 0,
                                    VART_VIRTIO_PCI_COMMON_SIZE, 0);
    if (ret < 0) {
        goto fail;
    }
    ret = virtio_pci_add_capability(device, VIRTIO_PCI_CAP_NOTIFY_CFG,
                                    VART_VIRTIO_PCI_NOTIFY_OFFSET,
                                    queue_count * VART_VIRTIO_PCI_NOTIFY_MULTIPLIER,
                                    VART_VIRTIO_PCI_NOTIFY_MULTIPLIER);
    if (ret < 0) {
        goto fail;
    }
    ret = virtio_pci_add_capability(device, VIRTIO_PCI_CAP_ISR_CFG,
                                    VART_VIRTIO_PCI_ISR_OFFSET, 1, 0);
    if (ret < 0) {
        goto fail;
    }
    ret = virtio_pci_add_capability(device, VIRTIO_PCI_CAP_DEVICE_CFG,
                                    VART_VIRTIO_PCI_DEVICE_OFFSET,
                                    VART_VIRTIO_PCI_DEVICE_SIZE, 0);
    if (ret < 0) {
        goto fail;
    }
    device->initialized = true;
    return 0;

fail:
    free(device->queue_state);
    device->queue_state = NULL;
    vart_virtio_device_destroy(&device->virtio);
    return ret;
}

int vart_virtio_pci_device_destroy(VartVirtioPciDevice *device)
{
    if (device == NULL) {
        return -EINVAL;
    }
    if (device->pci.parent.bus != NULL || device->pci.parent.realized) {
        return -EBUSY;
    }
    free(device->queue_state);
    device->queue_state = NULL;
    vart_virtio_device_destroy(&device->virtio);
    device->initialized = false;
    return 0;
}

int vart_virtio_pci_device_set_class_code(VartVirtioPciDevice *device,
                                          uint8_t class_code)
{
    if (device == NULL) {
        return -EINVAL;
    }
    if (device->pci.parent.bus != NULL || device->pci.parent.realized) {
        return -EBUSY;
    }
    device->pci.config_space[0x0b] = class_code;
    return 0;
}

int vart_virtio_pci_raise_interrupt(VartVirtioPciDevice *device,
                                    bool configuration_change)
{
    if (device == NULL || !device->initialized) {
        return -EINVAL;
    }
    device->isr_status |= configuration_change ? 2 : 1;
    return 0;
}
