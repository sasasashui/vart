#ifndef VART_DEVICE_H
#define VART_DEVICE_H

#include <stdbool.h>

#include "vart/address-space.h"

typedef struct VartBus VartBus;
typedef struct VartDevice VartDevice;

typedef struct VartDeviceOps {
    int (*realize)(VartDevice *device);
    void (*unrealize)(VartDevice *device);
    void (*reset)(VartDevice *device);
} VartDeviceOps;

struct VartDevice {
    const char *name;
    VartBus *bus;
    VartAddressSpace *dma_as;
    const VartDeviceOps *ops;
    void *opaque;
    bool realized;
};

void vart_device_init(VartDevice *device, const char *name,
                      const VartDeviceOps *ops, void *opaque,
                      VartAddressSpace *dma_as);
int vart_device_realize(VartDevice *device);
void vart_device_unrealize(VartDevice *device);
int vart_device_reset(VartDevice *device);

#endif
