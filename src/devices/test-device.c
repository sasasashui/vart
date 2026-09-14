#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "vart/devices/test-device.h"

enum {
    TEST_REG_OUTPUT = 0x00,
    TEST_REG_SCRATCH = 0x08,
    TEST_REG_STATUS = 0x10,
    TEST_REG_VERSION = 0x18,
    TEST_REG_IRQ_PULSE = 0x1c,
};

static int test_device_read(void *opaque, uint64_t offset, unsigned int size,
                            uint64_t *value)
{
    VartTestDevice *device = opaque;

    if (offset == TEST_REG_SCRATCH && size == 8) {
        *value = device->scratch;
        return 0;
    }
    if (offset == TEST_REG_STATUS && size == 4) {
        *value = device->status;
        return 0;
    }
    if (offset == TEST_REG_VERSION && size == 4) {
        *value = VART_TEST_DEVICE_VERSION;
        return 0;
    }
    return -EINVAL;
}

static int test_device_write(void *opaque, uint64_t offset, unsigned int size,
                             uint64_t value)
{
    VartTestDevice *device = opaque;

    if (offset == TEST_REG_OUTPUT && size == 1) {
        if (device->output != NULL) {
            device->output(device->output_opaque, value);
        }
        return 0;
    }
    if (offset == TEST_REG_SCRATCH && size == 8) {
        device->scratch = value;
        return 0;
    }
    if (offset == TEST_REG_STATUS && size == 4 &&
        (value == VART_TEST_STATUS_PASS || value == VART_TEST_STATUS_FAIL)) {
        device->status = value;
        return 0;
    }
    if (offset == TEST_REG_IRQ_PULSE && size == 4 && value == 1) {
        if (device->irq == NULL) {
            return -ENODEV;
        }
        return vart_irq_pulse(device->irq);
    }
    return -EINVAL;
}

static const VartMmioOps test_device_ops = {
    .read = test_device_read,
    .write = test_device_write,
};

int vart_test_device_init(VartTestDevice *device, uint64_t base,
                          VartTestOutput output, void *output_opaque)
{
    memset(device, 0, sizeof(*device));
    device->output = output;
    device->output_opaque = output_opaque;
    return vart_address_region_init_mmio(&device->region, base,
                                         VART_TEST_DEVICE_SIZE, 0, device,
                                         &test_device_ops, device);
}

void vart_test_device_reset(VartTestDevice *device)
{
    device->scratch = 0;
    device->status = VART_TEST_STATUS_NONE;
}

void vart_test_device_connect_irq(VartTestDevice *device, VartIrq *irq)
{
    device->irq = irq;
}
