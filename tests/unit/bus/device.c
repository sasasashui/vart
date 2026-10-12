#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "vart/bus.h"

typedef struct DeviceState {
    int realize_count;
    int unrealize_count;
    int reset_count;
    bool fail_realize;
} DeviceState;

static int test_realize(VartDevice *device)
{
    DeviceState *state = device->opaque;

    state->realize_count++;
    return state->fail_realize ? -EIO : 0;
}

static void test_unrealize(VartDevice *device)
{
    DeviceState *state = device->opaque;

    state->unrealize_count++;
}

static void test_reset(VartDevice *device)
{
    DeviceState *state = device->opaque;

    state->reset_count++;
}

static const VartDeviceOps test_ops = {
    .realize = test_realize,
    .unrealize = test_unrealize,
    .reset = test_reset,
};

int main(void)
{
    VartAddressSpace address_space;
    VartBus bus;
    VartDevice device;
    VartDevice second;
    DeviceState state = { 0 };
    DeviceState second_state = { 0 };

    vart_address_space_init(&address_space);
    vart_bus_init(&bus, "system-bus", NULL, &address_space);
    vart_device_init(&device, "test-device", &test_ops, &state,
                     &address_space);
    vart_device_init(&second, "second-device", &test_ops, &second_state,
                     NULL);
    if (vart_bus_add_device(&bus, &device) < 0 ||
        vart_bus_add_device(&bus, &device) != -EALREADY ||
        bus.device_count != 1 || device.bus != &bus ||
        device.dma_as != &address_space ||
        vart_device_realize(&device) < 0 || !device.realized ||
        state.realize_count != 1 ||
        vart_device_realize(&device) != -EALREADY ||
        vart_device_reset(&device) < 0 || state.reset_count != 1 ||
        vart_bus_remove_device(&bus, &device) != -EBUSY) {
        fprintf(stderr, "not ok - bus and device lifecycle\n");
        return EXIT_FAILURE;
    }
    vart_device_unrealize(&device);
    if (device.realized || state.unrealize_count != 1 ||
        vart_bus_remove_device(&bus, &device) < 0 ||
        device.bus != NULL || bus.device_count != 0 ||
        vart_bus_add_device(&bus, &second) < 0 ||
        vart_bus_destroy(&bus) != -EBUSY) {
        fprintf(stderr, "not ok - bus detach and destroy\n");
        return EXIT_FAILURE;
    }
    second_state.fail_realize = true;
    if (vart_device_realize(&second) != -EIO || second.realized ||
        second_state.realize_count != 1 ||
        vart_bus_remove_device(&bus, &second) != 0 ||
        vart_bus_destroy(&bus) != 0) {
        fprintf(stderr, "not ok - realize failure rollback\n");
        return EXIT_FAILURE;
    }
    vart_address_space_destroy(&address_space);
    puts("ok - bus and device lifecycle");
    return EXIT_SUCCESS;
}
