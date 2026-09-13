#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "vart/devices/test-device.h"

typedef struct Output { unsigned char value; unsigned int count; } Output;

static void capture(void *opaque, unsigned char value)
{
    Output *output = opaque;
    output->value = value;
    output->count++;
}

int main(void)
{
    VartAddressSpace as; VartTestDevice device; Output output = { 0 };
    uint64_t value;

    vart_address_space_init(&as);
    if (vart_test_device_init(&device, 0x1000, capture, &output) < 0 ||
        vart_address_space_add(&as, &device.region) < 0 ||
        vart_address_space_write(&as, 0x1008, 8,
                                 UINT64_C(0x1122334455667788)) < 0 ||
        vart_address_space_read(&as, 0x1008, 8, &value) < 0 ||
        value != UINT64_C(0x1122334455667788) ||
        vart_address_space_write(&as, 0x1000, 1, 'K') < 0 ||
        output.count != 1 || output.value != 'K' ||
        vart_address_space_read(&as, 0x1018, 4, &value) < 0 ||
        value != VART_TEST_DEVICE_VERSION ||
        vart_address_space_write(&as, 0x1010, 4,
                                 VART_TEST_STATUS_PASS) < 0 ||
        device.status != VART_TEST_STATUS_PASS) {
        fprintf(stderr, "not ok - test device register behavior\n");
        return EXIT_FAILURE;
    }
    vart_test_device_reset(&device);
    if (device.scratch != 0 || device.status != VART_TEST_STATUS_NONE ||
        vart_address_space_write(&as, 0x1010, 4, 3) != -EINVAL ||
        vart_address_space_read(&as, 0x1000, 1, &value) != -EINVAL) {
        fprintf(stderr, "not ok - test device validation and reset\n");
        return EXIT_FAILURE;
    }
    vart_address_space_destroy(&as);
    puts("ok - emulate VART test device registers");
    return EXIT_SUCCESS;
}
