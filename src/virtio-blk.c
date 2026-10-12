#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "vart/virtio-blk.h"

#define VIRTIO_F_VERSION_1 UINT64_C(32)
#define VIRTIO_BLK_F_RO UINT64_C(5)
#define VIRTIO_BLK_F_BLK_SIZE UINT64_C(6)
#define VIRTIO_BLK_F_FLUSH UINT64_C(9)

#define VIRTIO_BLK_T_IN UINT32_C(0)
#define VIRTIO_BLK_T_OUT UINT32_C(1)
#define VIRTIO_BLK_T_FLUSH UINT32_C(4)

#define VIRTIO_BLK_S_OK UINT8_C(0)
#define VIRTIO_BLK_S_IOERR UINT8_C(1)
#define VIRTIO_BLK_S_UNSUPP UINT8_C(2)

#define VIRTIO_BLK_REQUEST_HEADER_SIZE 16

static int virtio_blk_dma_read(void *opaque, uint64_t address, void *data,
                               size_t size)
{
    VartVirtioBlkDevice *device = opaque;

    return vart_pci_device_dma_read(&device->pci.pci, address, data, size);
}

static int virtio_blk_dma_write(void *opaque, uint64_t address,
                                const void *data, size_t size)
{
    VartVirtioBlkDevice *device = opaque;

    return vart_pci_device_dma_write(&device->pci.pci, address, data, size);
}

static uint32_t virtio_blk_load_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | (uint32_t)data[1] << 8 |
           (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

static uint64_t virtio_blk_load_le64(const uint8_t *data)
{
    uint64_t value = 0;

    for (unsigned int i = 0; i < sizeof(value); i++) {
        value |= (uint64_t)data[i] << (i * 8);
    }
    return value;
}

static int virtio_blk_config_read(VartVirtioPciDevice *pci, uint64_t offset,
                                  unsigned int size, uint64_t *value,
                                  void *opaque)
{
    VartVirtioBlkDevice *device = opaque;

    (void)pci;
    *value = 0;
    if (offset == 0 && size == 8) {
        *value = device->capacity_sectors;
    } else if (offset == 8 && size == 4) {
        *value = VART_VIRTIO_BLK_MAX_SEGMENT;
    } else if (offset == 12 && size == 4) {
        *value = 128;
    } else if (offset == 20 && size == 4) {
        *value = device->block_size;
    } else {
        return -EINVAL;
    }
    return 0;
}

static int virtio_blk_config_write(VartVirtioPciDevice *pci, uint64_t offset,
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

static int virtio_blk_write_status(VartVirtioBlkDevice *device,
                                   const VartVirtioDescriptor *status_desc,
                                   uint8_t status)
{
    return virtio_blk_dma_write(device, status_desc->address, &status, 1);
}

static bool virtio_blk_offset_valid(const VartVirtioBlkDevice *device,
                                    uint64_t sector, uint64_t bytes,
                                    off_t *offset)
{
    uint64_t byte_offset;
    uint64_t capacity_bytes;
    off_t converted;

    if (sector > UINT64_MAX / VART_VIRTIO_BLK_SECTOR_SIZE) {
        return false;
    }
    if (device->capacity_sectors > UINT64_MAX /
                                     VART_VIRTIO_BLK_SECTOR_SIZE) {
        return false;
    }
    byte_offset = sector * VART_VIRTIO_BLK_SECTOR_SIZE;
    capacity_bytes = device->capacity_sectors * VART_VIRTIO_BLK_SECTOR_SIZE;
    if (byte_offset > UINT64_MAX - bytes ||
        byte_offset + bytes > capacity_bytes) {
        return false;
    }
    converted = (off_t)byte_offset;
    if ((uint64_t)converted != byte_offset) {
        return false;
    }
    *offset = converted;
    return true;
}

/* Keep request validation separate from the synchronous file I/O path. */
static int virtio_blk_io(VartVirtioBlkDevice *device,
                         const VartVirtioChain *chain, uint32_t type,
                         uint64_t sector, uint32_t *used_length)
{
    uint64_t total = 0;
    off_t offset;
    bool to_guest = type == VIRTIO_BLK_T_IN;

    for (unsigned int i = 1; i + 1 < chain->count; i++) {
        const VartVirtioDescriptor *descriptor = &chain->descriptors[i];

        if (descriptor->length == 0 ||
            descriptor->length > VART_VIRTIO_BLK_MAX_SEGMENT ||
            (!!(descriptor->flags & VART_VIRTIO_DESC_F_WRITE) != to_guest) ||
            total > UINT64_MAX - descriptor->length) {
            return -EINVAL;
        }
        total += descriptor->length;
    }
    if (total == 0 || total % VART_VIRTIO_BLK_SECTOR_SIZE != 0 ||
        total > UINT32_MAX ||
        !virtio_blk_offset_valid(device, sector, total, &offset)) {
        return -EINVAL;
    }

    for (unsigned int i = 1; i + 1 < chain->count; i++) {
        const VartVirtioDescriptor *descriptor = &chain->descriptors[i];
        uint8_t *buffer = malloc(descriptor->length);
        size_t done = 0;

        if (buffer == NULL) {
            return -ENOMEM;
        }
        if (to_guest) {
            while (done < descriptor->length) {
                ssize_t count = pread(device->fd, buffer + done,
                                      descriptor->length - done, offset);
                if (count <= 0) {
                    free(buffer);
                    return count == 0 ? -EIO : -errno;
                }
                done += (size_t)count;
                offset += count;
            }
            if (virtio_blk_dma_write(device, descriptor->address, buffer,
                                     descriptor->length) < 0) {
                free(buffer);
                return -EIO;
            }
        } else {
            if (virtio_blk_dma_read(device, descriptor->address, buffer,
                                    descriptor->length) < 0) {
                free(buffer);
                return -EIO;
            }
            while (done < descriptor->length) {
                ssize_t count = pwrite(device->fd, buffer + done,
                                       descriptor->length - done, offset);
                if (count <= 0) {
                    free(buffer);
                    return count == 0 ? -EIO : -errno;
                }
                done += (size_t)count;
                offset += count;
            }
        }
        free(buffer);
        *used_length += descriptor->length;
    }
    return 0;
}

static int virtio_blk_process_request(VartVirtioBlkDevice *device,
                                       const VartVirtioChain *chain)
{
    const VartVirtioDescriptor *header_desc;
    const VartVirtioDescriptor *status_desc;
    uint8_t header[VIRTIO_BLK_REQUEST_HEADER_SIZE];
    uint32_t type;
    uint64_t sector;
    uint32_t used_length = 0;
    uint8_t status = VIRTIO_BLK_S_UNSUPP;
    int ret;

    if (chain->count < 2) {
        return -EPROTO;
    }
    header_desc = &chain->descriptors[0];
    status_desc = &chain->descriptors[chain->count - 1];
    if (header_desc->length < sizeof(header) ||
        (header_desc->flags & VART_VIRTIO_DESC_F_WRITE) ||
        status_desc->length == 0 ||
        !(status_desc->flags & VART_VIRTIO_DESC_F_WRITE)) {
        return -EPROTO;
    }
    ret = virtio_blk_dma_read(device, header_desc->address, header,
                              sizeof(header));
    if (ret < 0) {
        return ret;
    }
    type = virtio_blk_load_le32(header);
    sector = virtio_blk_load_le64(header + 8);
    if (type == VIRTIO_BLK_T_IN || type == VIRTIO_BLK_T_OUT) {
        if (type == VIRTIO_BLK_T_OUT && device->readonly) {
            status = VIRTIO_BLK_S_IOERR;
        } else {
            ret = virtio_blk_io(device, chain, type, sector, &used_length);
            status = ret < 0 ? VIRTIO_BLK_S_IOERR : VIRTIO_BLK_S_OK;
        }
    } else if (type == VIRTIO_BLK_T_FLUSH &&
               (device->pci.virtio.driver_features & (UINT64_C(1) <<
                                                       VIRTIO_BLK_F_FLUSH))) {
        ret = fsync(device->fd);
        status = ret < 0 ? VIRTIO_BLK_S_IOERR : VIRTIO_BLK_S_OK;
    }
    ret = virtio_blk_write_status(device, status_desc, status);
    if (ret < 0) {
        return ret;
    }
    return vart_virtio_queue_push_used(&device->pci.virtio, 0, chain->head,
                                       used_length);
}

static int virtio_blk_notify(VartVirtioPciDevice *pci, unsigned int queue_index,
                             void *opaque)
{
    VartVirtioBlkDevice *device = opaque;
    bool completed = false;
    int ret;

    if (queue_index != 0) {
        return -EINVAL;
    }
    for (;;) {
        VartVirtioChain chain;

        ret = vart_virtio_queue_pop(&device->pci.virtio, queue_index, &chain);
        if (ret == -EAGAIN) {
            break;
        }
        if (ret < 0) {
            return ret;
        }
        ret = virtio_blk_process_request(device, &chain);
        if (ret < 0) {
            return ret;
        }
        completed = true;
    }
    if (completed) {
        ret = vart_virtio_pci_raise_interrupt(pci, false);
    }
    return completed ? ret : 0;
}

int vart_virtio_blk_init(VartVirtioBlkDevice *device, const char *path,
                         bool readonly, VartAddressSpace *dma_as)
{
    struct stat statbuf;
    VartVirtioDmaOps dma;
    VartVirtioPciConfigOps config_ops;
    uint64_t features;
    int flags;
    int ret;

    if (device == NULL || path == NULL) {
        return -EINVAL;
    }
    memset(device, 0, sizeof(*device));
    flags = readonly ? O_RDONLY : O_RDWR;
    device->fd = open(path, flags | O_CLOEXEC);
    if (device->fd < 0) {
        return -errno;
    }
    if (fstat(device->fd, &statbuf) < 0 || !S_ISREG(statbuf.st_mode) ||
        statbuf.st_size < 0 ||
        (uint64_t)statbuf.st_size / VART_VIRTIO_BLK_SECTOR_SIZE == 0) {
        ret = -EINVAL;
        goto fail_fd;
    }
    device->capacity_sectors = (uint64_t)statbuf.st_size /
                               VART_VIRTIO_BLK_SECTOR_SIZE;
    device->block_size = VART_VIRTIO_BLK_SECTOR_SIZE;
    device->readonly = readonly;
    features = UINT64_C(1) << VIRTIO_F_VERSION_1 |
               UINT64_C(1) << VIRTIO_BLK_F_BLK_SIZE |
               UINT64_C(1) << VIRTIO_BLK_F_FLUSH;
    if (readonly) {
        features |= UINT64_C(1) << VIRTIO_BLK_F_RO;
    }
    dma = (VartVirtioDmaOps){
        .read = virtio_blk_dma_read,
        .write = virtio_blk_dma_write,
        .opaque = device,
    };
    config_ops = (VartVirtioPciConfigOps){
        .read = virtio_blk_config_read,
        .write = virtio_blk_config_write,
        .opaque = device,
    };
    ret = vart_virtio_pci_device_init(&device->pci, 2, features, 1, &dma,
                                      &config_ops, virtio_blk_notify, device);
    if (ret < 0) {
        goto fail_fd;
    }
    ret = vart_virtio_pci_device_set_class_code(&device->pci, 0x01);
    if (ret < 0) {
        vart_virtio_pci_device_destroy(&device->pci);
        goto fail_fd;
    }
    ret = vart_pci_device_set_dma_as(&device->pci.pci, dma_as);
    if (ret < 0) {
        vart_virtio_pci_device_destroy(&device->pci);
        goto fail_fd;
    }
    device->initialized = true;
    return 0;

fail_fd:
    close(device->fd);
    device->fd = -1;
    return ret;
}

int vart_virtio_blk_destroy(VartVirtioBlkDevice *device)
{
    int ret;

    if (device == NULL || !device->initialized) {
        return -EINVAL;
    }
    ret = vart_virtio_pci_device_destroy(&device->pci);
    if (ret < 0) {
        return ret;
    }
    close(device->fd);
    device->fd = -1;
    device->initialized = false;
    return 0;
}
