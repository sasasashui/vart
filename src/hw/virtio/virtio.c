#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "vart/virtio.h"

#define VIRTIO_DESCRIPTOR_SIZE 16
#define VIRTIO_AVAILABLE_HEADER_SIZE 4
#define VIRTIO_USED_HEADER_SIZE 4
#define VIRTIO_USED_ELEMENT_SIZE 8

static uint16_t virtio_load_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | (uint16_t)data[1] << 8;
}

static uint32_t virtio_load_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | (uint32_t)data[1] << 8 |
           (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

static uint64_t virtio_load_le64(const uint8_t *data)
{
    uint64_t value = 0;
    unsigned int i;

    for (i = 0; i < sizeof(value); i++) {
        value |= (uint64_t)data[i] << (i * 8);
    }
    return value;
}

static void virtio_store_le16(uint8_t *data, uint16_t value)
{
    data[0] = value;
    data[1] = value >> 8;
}

static void virtio_store_le32(uint8_t *data, uint32_t value)
{
    data[0] = value;
    data[1] = value >> 8;
    data[2] = value >> 16;
    data[3] = value >> 24;
}

static bool virtio_address_range_valid(uint64_t address, uint64_t size)
{
    return size == 0 || address <= UINT64_MAX - (size - 1);
}

static int virtio_queue_get(VartVirtioDevice *device,
                            unsigned int queue_index, VartVirtioQueue **queue)
{
    if (device == NULL || queue == NULL || device->queues == NULL ||
        queue_index >= device->queue_count) {
        return -EINVAL;
    }
    *queue = &device->queues[queue_index];
    if (!(*queue)->ready) {
        return -ENODEV;
    }
    return 0;
}

static int virtio_queue_read(const VartVirtioQueue *queue, uint64_t address,
                             void *data, size_t size)
{
    if (queue->dma.read == NULL) {
        return -ENOSYS;
    }
    return queue->dma.read(queue->dma.opaque, address, data, size);
}

static int virtio_queue_write(const VartVirtioQueue *queue, uint64_t address,
                              const void *data, size_t size)
{
    if (queue->dma.write == NULL) {
        return -ENOSYS;
    }
    return queue->dma.write(queue->dma.opaque, address, data, size);
}

int vart_virtio_device_init(VartVirtioDevice *device, uint32_t device_id,
                            uint64_t device_features, unsigned int queue_count,
                            const VartVirtioDmaOps *dma)
{
    if (device == NULL || queue_count == 0 ||
        queue_count > VART_VIRTIO_MAX_QUEUES || dma == NULL ||
        dma->read == NULL || dma->write == NULL) {
        return -EINVAL;
    }
    memset(device, 0, sizeof(*device));
    device->queues = calloc(queue_count, sizeof(*device->queues));
    if (device->queues == NULL) {
        return -ENOMEM;
    }
    device->device_id = device_id;
    device->device_features = device_features;
    device->queue_count = queue_count;
    for (unsigned int i = 0; i < queue_count; i++) {
        device->queues[i].dma = *dma;
    }
    return 0;
}

void vart_virtio_device_destroy(VartVirtioDevice *device)
{
    if (device == NULL) {
        return;
    }
    free(device->queues);
    memset(device, 0, sizeof(*device));
}

int vart_virtio_device_set_features(VartVirtioDevice *device,
                                    uint64_t driver_features)
{
    if (device == NULL ||
        (driver_features & ~device->device_features) != 0) {
        return -EINVAL;
    }
    device->driver_features = driver_features;
    return 0;
}

int vart_virtio_device_set_status(VartVirtioDevice *device, uint8_t status)
{
    unsigned int i;

    if (device == NULL || (status & ~VART_VIRTIO_STATUS_MASK) != 0) {
        return -EINVAL;
    }
    if (status == 0) {
        for (i = 0; i < device->queue_count; i++) {
            device->queues[i].ready = false;
            device->queues[i].last_avail = 0;
            device->queues[i].next_used = 0;
        }
        device->driver_features = 0;
    }
    device->status = status;
    return 0;
}

int vart_virtio_queue_configure(VartVirtioDevice *device,
                                unsigned int queue_index, uint16_t queue_size,
                                uint64_t descriptor_address,
                                uint64_t available_address,
                                uint64_t used_address)
{
    VartVirtioQueue *queue;
    uint64_t descriptor_size;
    uint64_t available_size;
    uint64_t used_size;
    if (device == NULL || device->queues == NULL ||
        queue_index >= device->queue_count || queue_size == 0 ||
        queue_size > VART_VIRTIO_MAX_QUEUE_SIZE ||
        (queue_size & (queue_size - 1)) != 0 ||
        (descriptor_address & 15) != 0 || (available_address & 1) != 0 ||
        (used_address & 3) != 0) {
        return -EINVAL;
    }
    queue = &device->queues[queue_index];
    if (queue->ready) {
        return -EBUSY;
    }
    descriptor_size = (uint64_t)queue_size * VIRTIO_DESCRIPTOR_SIZE;
    available_size = VIRTIO_AVAILABLE_HEADER_SIZE +
                     (uint64_t)queue_size * sizeof(uint16_t);
    used_size = VIRTIO_USED_HEADER_SIZE +
                (uint64_t)queue_size * VIRTIO_USED_ELEMENT_SIZE;
    if (!virtio_address_range_valid(descriptor_address, descriptor_size) ||
        !virtio_address_range_valid(available_address, available_size) ||
        !virtio_address_range_valid(used_address, used_size)) {
        return -EOVERFLOW;
    }
    queue->size = queue_size;
    queue->last_avail = 0;
    queue->next_used = 0;
    queue->descriptor_address = descriptor_address;
    queue->available_address = available_address;
    queue->used_address = used_address;
    queue->ready = true;
    return 0;
}

int vart_virtio_queue_reset(VartVirtioDevice *device,
                            unsigned int queue_index)
{
    VartVirtioQueue *queue;

    if (device == NULL || device->queues == NULL ||
        queue_index >= device->queue_count) {
        return -EINVAL;
    }
    queue = &device->queues[queue_index];
    queue->ready = false;
    queue->size = 0;
    queue->last_avail = 0;
    queue->next_used = 0;
    return 0;
}

int vart_virtio_queue_pop(VartVirtioDevice *device, unsigned int queue_index,
                          VartVirtioChain *chain)
{
    VartVirtioQueue *queue;
    uint8_t raw_available_index[sizeof(uint16_t)];
    uint8_t raw_head[sizeof(uint16_t)];
    uint8_t raw_descriptor[VIRTIO_DESCRIPTOR_SIZE];
    bool visited[VART_VIRTIO_MAX_QUEUE_SIZE] = { false };
    uint16_t available_index;
    uint16_t head;
    uint16_t descriptor_index;
    uint16_t pending;
    unsigned int count = 0;
    int ret;

    if (chain == NULL) {
        return -EINVAL;
    }
    ret = virtio_queue_get(device, queue_index, &queue);
    if (ret < 0) {
        return ret;
    }
    ret = virtio_queue_read(queue, queue->available_address + 2,
                            raw_available_index,
                            sizeof(raw_available_index));
    if (ret < 0) {
        return ret;
    }
    available_index = virtio_load_le16(raw_available_index);
    if (available_index == queue->last_avail) {
        return -EAGAIN;
    }
    pending = (uint16_t)(available_index - queue->last_avail);
    if (pending > queue->size) {
        return -EPROTO;
    }
    ret = virtio_queue_read(
        queue, queue->available_address + VIRTIO_AVAILABLE_HEADER_SIZE +
                   (queue->last_avail % queue->size) * sizeof(uint16_t),
        raw_head, sizeof(raw_head));
    if (ret < 0) {
        return ret;
    }
    head = virtio_load_le16(raw_head);
    if (head >= queue->size) {
        return -EPROTO;
    }
    memset(chain, 0, sizeof(*chain));
    chain->head = head;
    descriptor_index = head;
    for (;;) {
        uint64_t descriptor_address = queue->descriptor_address +
                                      (uint64_t)descriptor_index *
                                      VIRTIO_DESCRIPTOR_SIZE;
        VartVirtioDescriptor *descriptor;

        if (visited[descriptor_index]) {
            return -ELOOP;
        }
        visited[descriptor_index] = true;
        ret = virtio_queue_read(queue, descriptor_address, raw_descriptor,
                                sizeof(raw_descriptor));
        if (ret < 0) {
            return ret;
        }
        descriptor = &chain->descriptors[count++];
        descriptor->address = virtio_load_le64(raw_descriptor);
        descriptor->length = virtio_load_le32(raw_descriptor + 8);
        descriptor->flags = virtio_load_le16(raw_descriptor + 12);
        descriptor->next = virtio_load_le16(raw_descriptor + 14);
        if (descriptor->flags & VART_VIRTIO_DESC_F_INDIRECT) {
            return -ENOTSUP;
        }
        if (descriptor->flags & ~VART_VIRTIO_DESC_F_MASK) {
            return -EPROTO;
        }
        if (!(descriptor->flags & VART_VIRTIO_DESC_F_NEXT)) {
            break;
        }
        if (descriptor->next >= queue->size ||
            count == VART_VIRTIO_MAX_QUEUE_SIZE) {
            return -EPROTO;
        }
        descriptor_index = descriptor->next;
    }
    chain->count = count;
    queue->last_avail++;
    return 0;
}

int vart_virtio_queue_push_used(VartVirtioDevice *device,
                                unsigned int queue_index, uint16_t head,
                                uint32_t length)
{
    VartVirtioQueue *queue;
    uint8_t raw_used[VIRTIO_USED_ELEMENT_SIZE];
    uint16_t used_index;
    uint64_t used_element_address;
    int ret;

    ret = virtio_queue_get(device, queue_index, &queue);
    if (ret < 0) {
        return ret;
    }
    if (head >= queue->size) {
        return -EINVAL;
    }
    used_element_address = queue->used_address + VIRTIO_USED_HEADER_SIZE +
                           (uint64_t)(queue->next_used % queue->size) *
                           VIRTIO_USED_ELEMENT_SIZE;
    virtio_store_le32(raw_used, head);
    virtio_store_le32(raw_used + 4, length);
    ret = virtio_queue_write(queue, used_element_address, raw_used,
                             sizeof(raw_used));
    if (ret < 0) {
        return ret;
    }
    used_index = ++queue->next_used;
    virtio_store_le16(raw_used, used_index);
    return virtio_queue_write(queue, queue->used_address + 2, raw_used,
                              sizeof(used_index));
}
