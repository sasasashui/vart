#include <errno.h>
#include <linux/kvm.h>
#include <stdint.h>

#include "vart/kvm-irqfd.h"

static int vart_kvm_irqfd_ioctl(VartVm *vm,
                                const VartEventNotifier *notifier,
                                uint32_t gsi, uint32_t flags)
{
    struct kvm_irqfd irqfd;
    int fd;

    if (vm == NULL || vm->fd < 0 || vm->kvm == NULL ||
        !vm->kvm->irqfd || notifier == NULL) {
        return -EINVAL;
    }
    fd = vart_event_notifier_get_fd(notifier);
    if (fd < 0) {
        return -EINVAL;
    }
    irqfd = (struct kvm_irqfd) {
        .fd = (uint32_t)fd,
        .gsi = gsi,
        .flags = flags,
    };
    if (vart_vm_ioctl(vm, KVM_IRQFD,
                      (unsigned long)(uintptr_t)&irqfd) < 0) {
        return -errno;
    }
    return 0;
}

int vart_kvm_irqfd_assign(VartVm *vm, const VartEventNotifier *notifier,
                          uint32_t gsi)
{
    return vart_kvm_irqfd_ioctl(vm, notifier, gsi, 0);
}

int vart_kvm_irqfd_deassign(VartVm *vm, const VartEventNotifier *notifier,
                            uint32_t gsi)
{
    return vart_kvm_irqfd_ioctl(vm, notifier, gsi,
                                KVM_IRQFD_FLAG_DEASSIGN);
}
