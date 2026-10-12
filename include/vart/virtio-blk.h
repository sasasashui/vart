#ifndef VART_VIRTIO_BLK_H
#define VART_VIRTIO_BLK_H

#include <stdbool.h>
#include <stdint.h>

#include "vart/address-space.h"
#include "vart/virtio-pci.h"

#define VART_VIRTIO_BLK_SECTOR_SIZE UINT32_C(512)
#define VART_VIRTIO_BLK_MAX_SEGMENT (UINT32_C(1) << 20)

typedef struct VartVirtioBlkDevice {
    VartVirtioPciDevice pci;
    int fd;
    uint64_t capacity_sectors;
    uint32_t block_size;
    bool readonly;
    bool initialized;
} VartVirtioBlkDevice;

int vart_virtio_blk_init(VartVirtioBlkDevice *device, const char *path,
                         bool readonly, VartAddressSpace *dma_as);
int vart_virtio_blk_destroy(VartVirtioBlkDevice *device);

#endif
