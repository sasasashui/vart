#ifndef VART_KVM_DEVICE_H
#define VART_KVM_DEVICE_H

#include <stdint.h>

#include "vart/vm.h"

typedef struct VartKvmDevice {
    VartVm *vm;
    int fd;
    uint32_t type;
} VartKvmDevice;

int vart_kvm_device_create(VartKvmDevice *device, VartVm *vm,
                           uint32_t type);
void vart_kvm_device_destroy(VartKvmDevice *device);
int vart_kvm_device_has_attr(const VartKvmDevice *device,
                             uint32_t group, uint64_t attr);
int vart_kvm_device_get_attr(const VartKvmDevice *device,
                             uint32_t group, uint64_t attr, void *value);
int vart_kvm_device_set_attr(const VartKvmDevice *device,
                             uint32_t group, uint64_t attr,
                             const void *value);

#endif
