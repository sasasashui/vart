#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/kvm.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "vart/kvm.h"

int vart_kvm_check_extension(const VartKvm *kvm, int capability)
{
    int ret;

    ret = ioctl(kvm->fd, KVM_CHECK_EXTENSION, capability);
    return ret < 0 ? -errno : ret;
}

int vart_kvm_open(VartKvm *kvm)
{
    int ret;

    memset(kvm, 0, sizeof(*kvm));
    kvm->fd = -1;

    kvm->fd = open("/dev/kvm", O_RDWR | O_CLOEXEC);
    if (kvm->fd < 0) {
        return -errno;
    }

    ret = ioctl(kvm->fd, KVM_GET_API_VERSION, 0);
    if (ret < 0) {
        ret = -errno;
        goto fail;
    }
    kvm->api_version = ret;
    if (kvm->api_version != KVM_API_VERSION) {
        ret = -EPROTONOSUPPORT;
        goto fail;
    }

    ret = ioctl(kvm->fd, KVM_GET_VCPU_MMAP_SIZE, 0);
    if (ret < 0) {
        ret = -errno;
        goto fail;
    }
    kvm->vcpu_mmap_size = ret;
    if (kvm->vcpu_mmap_size < (int)sizeof(struct kvm_run)) {
        ret = -EOVERFLOW;
        goto fail;
    }

    kvm->recommended_vcpus =
        vart_kvm_check_extension(kvm, KVM_CAP_NR_VCPUS);
    kvm->max_vcpus = vart_kvm_check_extension(kvm, KVM_CAP_MAX_VCPUS);
    kvm->user_memory =
        vart_kvm_check_extension(kvm, KVM_CAP_USER_MEMORY) > 0;
    kvm->one_reg = vart_kvm_check_extension(kvm, KVM_CAP_ONE_REG) > 0;
    kvm->irqfd = vart_kvm_check_extension(kvm, KVM_CAP_IRQFD) > 0;
    kvm->ioeventfd = vart_kvm_check_extension(kvm, KVM_CAP_IOEVENTFD) > 0;
    kvm->immediate_exit =
        vart_kvm_check_extension(kvm, KVM_CAP_IMMEDIATE_EXIT) > 0;
    kvm->mp_state = vart_kvm_check_extension(kvm, KVM_CAP_MP_STATE) > 0;
    kvm->riscv_mp_state_reset =
        vart_kvm_check_extension(kvm, KVM_CAP_RISCV_MP_STATE_RESET) > 0;

    if (!kvm->user_memory || !kvm->one_reg) {
        ret = -ENOTSUP;
        goto fail;
    }

    return 0;

fail:
    vart_kvm_close(kvm);
    return ret;
}

void vart_kvm_close(VartKvm *kvm)
{
    if (kvm->fd >= 0) {
        close(kvm->fd);
    }
    kvm->fd = -1;
}
