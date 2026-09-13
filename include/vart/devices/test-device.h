#ifndef VART_DEVICES_TEST_DEVICE_H
#define VART_DEVICES_TEST_DEVICE_H

#include <stdint.h>

#include "vart/address-space.h"

#define VART_TEST_DEVICE_SIZE UINT64_C(0x20)
#define VART_TEST_STATUS_NONE UINT32_C(0)
#define VART_TEST_STATUS_PASS UINT32_C(1)
#define VART_TEST_STATUS_FAIL UINT32_C(2)
#define VART_TEST_DEVICE_VERSION UINT32_C(1)

typedef void (*VartTestOutput)(void *opaque, unsigned char value);

typedef struct VartTestDevice {
    VartAddressRegion region;
    uint64_t scratch;
    uint32_t status;
    VartTestOutput output;
    void *output_opaque;
} VartTestDevice;

int vart_test_device_init(VartTestDevice *device, uint64_t base,
                          VartTestOutput output, void *output_opaque);
void vart_test_device_reset(VartTestDevice *device);

#endif
