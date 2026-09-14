#include <errno.h>
#include <linux/kvm.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "vart/kvm-device.h"

int vart_kvm_device_create(VartKvmDevice *device, VartVm *vm,
                           uint32_t type)
{
    struct kvm_create_device create = {
        .type = type,
    };

    if (device == NULL || vm == NULL || vm->fd < 0) {
        return -EINVAL;
    }
    memset(device, 0, sizeof(*device));
    device->fd = -1;
    if (ioctl(vm->fd, KVM_CREATE_DEVICE, &create) < 0) {
        return -errno;
    }
    device->vm = vm;
    device->fd = create.fd;
    device->type = type;
    return 0;
}

void vart_kvm_device_destroy(VartKvmDevice *device)
{
    if (device == NULL) {
        return;
    }
    if (device->fd >= 0) {
        close(device->fd);
    }
    memset(device, 0, sizeof(*device));
    device->fd = -1;
}

static int device_attr_ioctl(const VartKvmDevice *device,
                             unsigned long request, uint32_t group,
                             uint64_t attr, const void *value)
{
    struct kvm_device_attr device_attr = {
        .group = group,
        .attr = attr,
        .addr = (uintptr_t)value,
    };

    if (device == NULL || device->fd < 0) {
        return -EINVAL;
    }
    if (ioctl(device->fd, request, &device_attr) < 0) {
        return -errno;
    }
    return 0;
}

int vart_kvm_device_has_attr(const VartKvmDevice *device,
                             uint32_t group, uint64_t attr)
{
    return device_attr_ioctl(device, KVM_HAS_DEVICE_ATTR, group, attr, NULL);
}

int vart_kvm_device_get_attr(const VartKvmDevice *device,
                             uint32_t group, uint64_t attr, void *value)
{
    if (value == NULL) {
        return -EINVAL;
    }
    return device_attr_ioctl(device, KVM_GET_DEVICE_ATTR, group, attr, value);
}

int vart_kvm_device_set_attr(const VartKvmDevice *device,
                             uint32_t group, uint64_t attr,
                             const void *value)
{
    return device_attr_ioctl(device, KVM_SET_DEVICE_ATTR, group, attr, value);
}
