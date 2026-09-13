#include <errno.h>
#include <linux/kvm.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "vart/vm.h"

int vart_vm_create(VartVm *vm, const VartKvm *kvm)
{
    memset(vm, 0, sizeof(*vm));
    vm->fd = -1;
    vm->kvm = kvm;

    vm->fd = ioctl(kvm->fd, KVM_CREATE_VM, 0);
    if (vm->fd < 0) {
        return -errno;
    }

    return 0;
}

void vart_vm_destroy(VartVm *vm)
{
    if (vm->fd >= 0) {
        close(vm->fd);
    }
    vm->fd = -1;
    vm->kvm = NULL;
}

int vart_vm_check_device(const VartVm *vm, unsigned int type)
{
    struct kvm_create_device device = {
        .type = type,
        .fd = 0,
        .flags = KVM_CREATE_DEVICE_TEST,
    };

    if (ioctl(vm->fd, KVM_CREATE_DEVICE, &device) == 0) {
        return 1;
    }
    if (errno == ENODEV) {
        return 0;
    }
    return -errno;
}
