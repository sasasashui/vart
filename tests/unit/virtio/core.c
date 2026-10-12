#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vart/virtio.h"

static void store_le16(uint8_t *data, uint16_t value)
{
    data[0] = value;
    data[1] = value >> 8;
}

static void store_le32(uint8_t *data, uint32_t value)
{
    data[0] = value;
    data[1] = value >> 8;
    data[2] = value >> 16;
    data[3] = value >> 24;
}

static void store_le64(uint8_t *data, uint64_t value)
{
    unsigned int i;

    for (i = 0; i < sizeof(value); i++) {
        data[i] = value >> (i * 8);
    }
}

static uint16_t load_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | (uint16_t)data[1] << 8;
}

static uint32_t load_le32(const uint8_t *data)
{
    return (uint32_t)data[0] | (uint32_t)data[1] << 8 |
           (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

static int dma_read(void *opaque, uint64_t address, void *data, size_t size)
{
    uint8_t *memory = opaque;

    if (address > 0x4000 || size > 0x4000 - address) {
        return -ERANGE;
    }
    memcpy(data, memory + address, size);
    return 0;
}

static int dma_write(void *opaque, uint64_t address, const void *data,
                     size_t size)
{
    uint8_t *memory = opaque;

    if (address > 0x4000 || size > 0x4000 - address) {
        return -ERANGE;
    }
    memcpy(memory + address, data, size);
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

int main(void)
{
    uint8_t memory[0x4000] = { 0 };
    const VartVirtioDmaOps dma = {
        .read = dma_read,
        .write = dma_write,
        .opaque = memory,
    };
    VartVirtioDevice device;
    VartVirtioChain chain;
    int ret;

    ret = vart_virtio_device_init(&device, 2, UINT64_C(0x5), 1, &dma);
    if (ret < 0 || check(vart_virtio_device_set_features(&device, 0x8) ==
                         -EINVAL, "reject unsupported features") < 0 ||
        check(vart_virtio_device_set_features(&device, 0x5) == 0,
              "negotiate device features") < 0 ||
        check(device.driver_features == 0x5, "record driver features") < 0 ||
        check(vart_virtio_device_set_status(
                  &device, VART_VIRTIO_STATUS_ACKNOWLEDGE |
                  VART_VIRTIO_STATUS_DRIVER | VART_VIRTIO_STATUS_DRIVER_OK) ==
                  0,
              "set device status") < 0 ||
        check(vart_virtio_queue_pop(&device, 0, &chain) == -ENODEV,
              "unconfigured queue is unavailable") < 0 ||
        check(vart_virtio_queue_configure(&device, 0, 3, 0x100, 0x1000,
                                          0x1100) == -EINVAL,
              "reject non-power-of-two queue") < 0 ||
        check(vart_virtio_queue_configure(&device, 0, 4, 0x100, 0x1000,
                                          0x1100) == 0,
              "configure split virtqueue") < 0) {
        vart_virtio_device_destroy(&device);
        return 1;
    }

    store_le64(memory + 0x100, 0x2000);
    store_le32(memory + 0x108, 4);
    store_le16(memory + 0x10c, VART_VIRTIO_DESC_F_NEXT);
    store_le16(memory + 0x10e, 1);
    store_le64(memory + 0x110, 0x2010);
    store_le32(memory + 0x118, 8);
    store_le16(memory + 0x11c, VART_VIRTIO_DESC_F_WRITE);
    store_le16(memory + 0x11e, 0);
    store_le16(memory + 0x1002, 1);
    store_le16(memory + 0x1004, 0);

    if (check(vart_virtio_queue_pop(&device, 0, &chain) == 0,
              "pop available descriptor chain") < 0 ||
        check(chain.head == 0 && chain.count == 2,
              "walk chained descriptors") < 0 ||
        check(chain.descriptors[0].address == 0x2000 &&
              chain.descriptors[0].length == 4 &&
              (chain.descriptors[0].flags & VART_VIRTIO_DESC_F_NEXT),
              "decode first descriptor") < 0 ||
        check(chain.descriptors[1].address == 0x2010 &&
              chain.descriptors[1].length == 8 &&
              (chain.descriptors[1].flags & VART_VIRTIO_DESC_F_WRITE),
              "decode writable descriptor") < 0 ||
        check(vart_virtio_queue_push_used(&device, 0, chain.head, 12) == 0,
              "publish used descriptor") < 0 ||
        check(load_le32(memory + 0x1104) == 0 &&
              load_le32(memory + 0x1108) == 12 &&
              load_le16(memory + 0x1102) == 1,
              "update used ring") < 0 ||
        check(vart_virtio_queue_pop(&device, 0, &chain) == -EAGAIN,
              "empty available ring") < 0) {
        vart_virtio_device_destroy(&device);
        return 1;
    }

    store_le16(memory + 0x1002, 2);
    store_le16(memory + 0x1006, 1);
    store_le16(memory + 0x11c, VART_VIRTIO_DESC_F_NEXT);
    store_le16(memory + 0x11e, 1);
    if (check(vart_virtio_queue_pop(&device, 0, &chain) == -ELOOP,
              "reject descriptor loop") < 0) {
        vart_virtio_device_destroy(&device);
        return 1;
    }
    store_le16(memory + 0x11c, VART_VIRTIO_DESC_F_WRITE);
    if (check(vart_virtio_queue_pop(&device, 0, &chain) == 0,
              "recover after malformed chain") < 0 ||
        check(vart_virtio_queue_reset(&device, 0) == 0,
              "reset virtqueue") < 0 ||
        check(vart_virtio_queue_pop(&device, 0, &chain) == -ENODEV,
              "reset queue is unavailable") < 0 ||
        check(vart_virtio_device_set_status(&device, 0) == 0 &&
              device.driver_features == 0,
              "reset device state") < 0) {
        vart_virtio_device_destroy(&device);
        return 1;
    }
    vart_virtio_device_destroy(&device);
    puts("ok - virtio split virtqueue core");
    return 0;
}
