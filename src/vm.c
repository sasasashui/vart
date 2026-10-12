#include <errno.h>
#include <linux/kvm.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "vart/vm.h"

int vart_vm_ioctl(const VartVm *vm, unsigned long request,
                  unsigned long arg)
{
    return ioctl(vm->fd, request, arg);
}

int vart_vm_create(VartVm *vm, const VartKvm *kvm)
{
    int ret;

    memset(vm, 0, sizeof(*vm));
    vm->fd = -1;
    vm->kvm = kvm;

    ret = vart_mutex_init_rank(&vm->big_lock, VART_LOCK_RANK_VM);
    if (ret < 0) {
        return ret;
    }

    vm->fd = vart_kvm_ioctl(kvm, KVM_CREATE_VM, 0);
    if (vm->fd < 0) {
        ret = -errno;
        vart_mutex_destroy(&vm->big_lock);
        return ret;
    }

    return 0;
}

void vart_vm_destroy(VartVm *vm)
{
    if (vm->fd >= 0) {
        close(vm->fd);
    }
    vart_mutex_destroy(&vm->big_lock);
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

    if (vart_vm_ioctl(vm, KVM_CREATE_DEVICE,
                      (unsigned long)(uintptr_t)&device) == 0) {
        return 1;
    }
    if (errno == ENODEV) {
        return 0;
    }
    return -errno;
}
