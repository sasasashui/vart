#include <errno.h>
#include <linux/kvm.h>
#include <stdint.h>

#include "vart/kvm-ioeventfd.h"

static int vart_kvm_ioeventfd_ioctl(VartVm *vm,
                                    const VartEventNotifier *notifier,
                                    uint64_t address, uint8_t length,
                                    uint32_t flags)
{
    struct kvm_ioeventfd ioeventfd;
    int fd;

    if (vm == NULL || vm->fd < 0 || vm->kvm == NULL ||
        !vm->kvm->ioeventfd || notifier == NULL) {
        return -EINVAL;
    }
    fd = vart_event_notifier_get_fd(notifier);
    if (fd < 0 || length == 0) {
        return -EINVAL;
    }
    ioeventfd = (struct kvm_ioeventfd) {
        .len = length,
        .addr = address,
        .fd = (uint32_t)fd,
        .flags = flags,
    };
    if (vart_vm_ioctl(vm, KVM_IOEVENTFD,
                      (unsigned long)(uintptr_t)&ioeventfd) < 0) {
        return -errno;
    }
    return 0;
}

int vart_kvm_ioeventfd_assign(VartVm *vm,
                              const VartEventNotifier *notifier,
                              uint64_t address, uint8_t length)
{
    return vart_kvm_ioeventfd_ioctl(vm, notifier, address, length, 0);
}

int vart_kvm_ioeventfd_deassign(VartVm *vm,
                                const VartEventNotifier *notifier,
                                uint64_t address, uint8_t length)
{
    return vart_kvm_ioeventfd_ioctl(vm, notifier, address, length,
                                    KVM_IOEVENTFD_FLAG_DEASSIGN);
}
