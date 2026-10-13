#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "vart/bus.h"

void vart_bus_init(VartBus *bus, const char *name, VartBus *parent,
                   VartAddressSpace *address_space)
{
    memset(bus, 0, sizeof(*bus));
    bus->name = name;
    bus->parent = parent;
    bus->address_space = address_space;
}

int vart_bus_destroy(VartBus *bus)
{
    if (bus == NULL) {
        return -EINVAL;
    }
    if (bus->device_count != 0) {
        return -EBUSY;
    }
    free(bus->devices);
    memset(bus, 0, sizeof(*bus));
    return 0;
}

int vart_bus_add_device(VartBus *bus, VartDevice *device)
{
    VartDevice **devices;
    size_t capacity;

    if (bus == NULL || device == NULL) {
        return -EINVAL;
    }
    if (device->bus != NULL) {
        return -EALREADY;
    }
    if (device->realized) {
        return -EBUSY;
    }
    if (bus->device_count == bus->device_capacity) {
        capacity = bus->device_capacity == 0 ? 4 : bus->device_capacity * 2;
        if (capacity < bus->device_capacity ||
            capacity > SIZE_MAX / sizeof(*devices)) {
            return -EOVERFLOW;
        }
        devices = realloc(bus->devices, capacity * sizeof(*devices));
        if (devices == NULL) {
            return -ENOMEM;
        }
        bus->devices = devices;
        bus->device_capacity = capacity;
    }
    bus->devices[bus->device_count++] = device;
    device->bus = bus;
    return 0;
}

int vart_bus_remove_device(VartBus *bus, VartDevice *device)
{
    size_t i;

    if (bus == NULL || device == NULL || device->bus != bus) {
        return -ENOENT;
    }
    if (device->realized) {
        return -EBUSY;
    }
    for (i = 0; i < bus->device_count; i++) {
        if (bus->devices[i] == device) {
            memmove(&bus->devices[i], &bus->devices[i + 1],
                    (bus->device_count - i - 1) * sizeof(*bus->devices));
            bus->device_count--;
            device->bus = NULL;
            return 0;
        }
    }
    return -ENOENT;
}
