#ifndef VART_BUS_H
#define VART_BUS_H

#include <stddef.h>

#include "vart/device.h"

struct VartBus {
    const char *name;
    VartBus *parent;
    VartAddressSpace *address_space;
    VartDevice **devices;
    size_t device_count;
    size_t device_capacity;
};

void vart_bus_init(VartBus *bus, const char *name, VartBus *parent,
                   VartAddressSpace *address_space);
int vart_bus_destroy(VartBus *bus);
int vart_bus_add_device(VartBus *bus, VartDevice *device);
int vart_bus_remove_device(VartBus *bus, VartDevice *device);

#endif
