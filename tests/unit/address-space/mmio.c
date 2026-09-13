#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "vart/address-space.h"

typedef struct TestDevice {
    uint64_t offset;
    uint64_t value;
    unsigned int size;
    int error;
} TestDevice;

static int test_read(void *opaque, uint64_t offset, unsigned int size,
                     uint64_t *value)
{
    TestDevice *device = opaque;

    device->offset = offset;
    device->size = size;
    *value = device->value;
    return device->error;
}

static int test_write(void *opaque, uint64_t offset, unsigned int size,
                      uint64_t value)
{
    TestDevice *device = opaque;

    device->offset = offset;
    device->size = size;
    device->value = value;
    return device->error;
}

int main(void)
{
    const VartMmioOps ops = { .read = test_read, .write = test_write };
    const VartMmioOps read_only = { .read = test_read };
    VartAddressRegion region;
    VartAddressRegion ram;
    VartAddressRegion ro;
    VartAddressSpace address_space;
    TestDevice device = { 0 };
    uint64_t value;

    vart_address_space_init(&address_space);
    if (vart_address_region_init_mmio(&region, 0x1000, 0x100, 0, &device,
                                      &ops, &device) != 0 ||
        vart_address_space_add(&address_space, &region) != 0) {
        return EXIT_FAILURE;
    }

    if (vart_address_space_write(&address_space, 0x1018, 4,
                                 UINT64_C(0xaabbccdd11223344)) != 0 ||
        device.offset != 0x18 || device.size != 4 ||
        device.value != UINT64_C(0x11223344)) {
        fprintf(stderr, "not ok - dispatch MMIO write\n");
        return EXIT_FAILURE;
    }

    device.value = UINT64_C(0xffeeddccbbaa5599);
    if (vart_address_space_read(&address_space, 0x1020, 2, &value) != 0 ||
        device.offset != 0x20 || device.size != 2 || value != 0x5599) {
        fprintf(stderr, "not ok - dispatch MMIO read\n");
        return EXIT_FAILURE;
    }

    device.error = -EIO;
    if (vart_address_space_read(&address_space, 0x1000, 1, &value) != -EIO ||
        vart_address_space_write(&address_space, 0x1000, 8, 0) != -EIO) {
        fprintf(stderr, "not ok - propagate MMIO callback error\n");
        return EXIT_FAILURE;
    }
    device.error = 0;

    vart_address_region_init(&ram, VART_REGION_RAM, 0x2000, 0x100, 0, NULL);
    vart_address_region_init_mmio(&ro, 0x3000, 0x100, 0, NULL,
                                  &read_only, &device);
    vart_address_space_add(&address_space, &ram);
    vart_address_space_add(&address_space, &ro);
    if (vart_address_space_read(&address_space, 0x1100, 1, &value) != -ENOENT ||
        vart_address_space_read(&address_space, 0x10ff, 2, &value) != -ENOENT ||
        vart_address_space_read(&address_space, 0x2000, 4, &value) != -EACCES ||
        vart_address_space_write(&address_space, 0x3000, 4, 0) != -ENOSYS ||
        vart_address_space_read(&address_space, 0x1000, 3, &value) != -EINVAL ||
        vart_address_space_read(&address_space, 0x1000, 1, NULL) != -EINVAL) {
        fprintf(stderr, "not ok - reject invalid MMIO access\n");
        return EXIT_FAILURE;
    }

    vart_address_space_destroy(&address_space);
    printf("ok - dispatch MMIO region accesses\n");
    return EXIT_SUCCESS;
}
