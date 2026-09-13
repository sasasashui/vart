#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "vart/exec.h"

typedef struct TestDevice {
    uint64_t value;
    unsigned int size;
} TestDevice;

static int test_write(void *opaque, uint64_t offset, unsigned int size,
                      uint64_t value)
{
    TestDevice *device = opaque;

    if (offset != 8) {
        return -EIO;
    }
    device->value = value;
    device->size = size;
    return 0;
}

int main(void)
{
    const VartMmioOps ops = { .write = test_write };
    VartVcpuExit exit = {
        .type = VART_VCPU_EXIT_MMIO,
        .mmio = {
            .address = 0x1008,
            .data = { 0x78, 0x56, 0x34, 0x12 },
            .size = 4,
            .is_write = true,
        },
    };
    VartAddressSpace address_space;
    VartAddressRegion region;
    VartExecution execution;
    TestDevice device = { 0 };

    vart_address_space_init(&address_space);
    vart_address_region_init_mmio(&region, 0x1000, 0x100, 0, &device,
                                  &ops, &device);
    vart_address_space_add(&address_space, &region);
    vart_execution_init(&execution, &address_space);

    if (vart_execution_handle_exit(&execution, NULL, &exit) != 0 ||
        device.value != UINT64_C(0x12345678) || device.size != 4) {
        fprintf(stderr, "not ok - dispatch MMIO write exit\n");
        return EXIT_FAILURE;
    }
    exit.mmio.size = 3;
    if (vart_execution_handle_exit(&execution, NULL, &exit) != -EINVAL) {
        fprintf(stderr, "not ok - reject invalid MMIO write exit\n");
        return EXIT_FAILURE;
    }

    vart_address_space_destroy(&address_space);
    printf("ok - dispatch MMIO write exit\n");
    return EXIT_SUCCESS;
}
