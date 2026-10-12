#include <errno.h>
#include <string.h>

#include "vart/device.h"

void vart_device_init(VartDevice *device, const char *name,
                      const VartDeviceOps *ops, void *opaque,
                      VartAddressSpace *dma_as)
{
    memset(device, 0, sizeof(*device));
    device->name = name;
    device->ops = ops;
    device->opaque = opaque;
    device->dma_as = dma_as;
}

int vart_device_realize(VartDevice *device)
{
    int ret;

    if (device == NULL || device->realized) {
        return -EALREADY;
    }
    if (device->ops != NULL && device->ops->realize != NULL) {
        ret = device->ops->realize(device);
        if (ret < 0) {
            return ret;
        }
    }
    device->realized = true;
    return 0;
}

void vart_device_unrealize(VartDevice *device)
{
    if (device == NULL || !device->realized) {
        return;
    }
    if (device->ops != NULL && device->ops->unrealize != NULL) {
        device->ops->unrealize(device);
    }
    device->realized = false;
}

int vart_device_reset(VartDevice *device)
{
    if (device == NULL || !device->realized) {
        return -EINVAL;
    }
    if (device->ops != NULL && device->ops->reset != NULL) {
        device->ops->reset(device);
    }
    return 0;
}
