#ifndef VART_VIRTIO_H
#define VART_VIRTIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VART_VIRTIO_STATUS_ACKNOWLEDGE UINT8_C(0x01)
#define VART_VIRTIO_STATUS_DRIVER UINT8_C(0x02)
#define VART_VIRTIO_STATUS_DRIVER_OK UINT8_C(0x04)
#define VART_VIRTIO_STATUS_FEATURES_OK UINT8_C(0x08)
#define VART_VIRTIO_STATUS_DEVICE_NEEDS_RESET UINT8_C(0x40)
#define VART_VIRTIO_STATUS_FAILED UINT8_C(0x80)
#define VART_VIRTIO_STATUS_MASK (VART_VIRTIO_STATUS_ACKNOWLEDGE | \
                                 VART_VIRTIO_STATUS_DRIVER | \
                                 VART_VIRTIO_STATUS_DRIVER_OK | \
                                 VART_VIRTIO_STATUS_FEATURES_OK | \
                                 VART_VIRTIO_STATUS_DEVICE_NEEDS_RESET | \
                                 VART_VIRTIO_STATUS_FAILED)

#define VART_VIRTIO_DESC_F_NEXT UINT16_C(0x0001)
#define VART_VIRTIO_DESC_F_WRITE UINT16_C(0x0002)
#define VART_VIRTIO_DESC_F_INDIRECT UINT16_C(0x0004)
#define VART_VIRTIO_DESC_F_MASK (VART_VIRTIO_DESC_F_NEXT | \
                                 VART_VIRTIO_DESC_F_WRITE | \
                                 VART_VIRTIO_DESC_F_INDIRECT)
#define VART_VIRTIO_MAX_QUEUE_SIZE 1024
#define VART_VIRTIO_MAX_QUEUES 256

typedef struct VartVirtioDevice VartVirtioDevice;
typedef struct VartVirtioQueue VartVirtioQueue;

typedef int (*VartVirtioDmaRead)(void *opaque, uint64_t address,
                                 void *data, size_t size);
typedef int (*VartVirtioDmaWrite)(void *opaque, uint64_t address,
                                  const void *data, size_t size);

typedef struct VartVirtioDmaOps {
    VartVirtioDmaRead read;
    VartVirtioDmaWrite write;
    void *opaque;
} VartVirtioDmaOps;

typedef struct VartVirtioDescriptor {
    uint64_t address;
    uint32_t length;
    uint16_t flags;
    uint16_t next;
} VartVirtioDescriptor;

typedef struct VartVirtioChain {
    uint16_t head;
    unsigned int count;
    VartVirtioDescriptor descriptors[VART_VIRTIO_MAX_QUEUE_SIZE];
} VartVirtioChain;

struct VartVirtioQueue {
    uint16_t size;
    uint16_t last_avail;
    uint16_t next_used;
    uint64_t descriptor_address;
    uint64_t available_address;
    uint64_t used_address;
    VartVirtioDmaOps dma;
    bool ready;
};

struct VartVirtioDevice {
    uint32_t device_id;
    uint64_t device_features;
    uint64_t driver_features;
    uint8_t status;
    VartVirtioQueue *queues;
    unsigned int queue_count;
};

int vart_virtio_device_init(VartVirtioDevice *device, uint32_t device_id,
                            uint64_t device_features, unsigned int queue_count,
                            const VartVirtioDmaOps *dma);
void vart_virtio_device_destroy(VartVirtioDevice *device);
int vart_virtio_device_set_features(VartVirtioDevice *device,
                                    uint64_t driver_features);
int vart_virtio_device_set_status(VartVirtioDevice *device, uint8_t status);
int vart_virtio_queue_configure(VartVirtioDevice *device,
                                unsigned int queue_index, uint16_t queue_size,
                                uint64_t descriptor_address,
                                uint64_t available_address,
                                uint64_t used_address);
int vart_virtio_queue_reset(VartVirtioDevice *device,
                            unsigned int queue_index);
int vart_virtio_queue_pop(VartVirtioDevice *device, unsigned int queue_index,
                          VartVirtioChain *chain);
int vart_virtio_queue_push_used(VartVirtioDevice *device,
                                unsigned int queue_index, uint16_t head,
                                uint32_t length);

#endif
