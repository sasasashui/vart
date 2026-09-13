#define _GNU_SOURCE

#include <errno.h>
#include <linux/kvm.h>
#include <asm/kvm.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "vart/vcpu.h"

#define RISCV_CORE_REG(name) \
    (KVM_REG_RISCV | KVM_REG_SIZE_U64 | KVM_REG_RISCV_CORE | \
     KVM_REG_RISCV_CORE_REG(name))

static uint64_t riscv_gpr_id(unsigned int index)
{
    /*
     * The RISC-V KVM core register array starts with PC at index zero,
     * followed by x1 through x31 in architectural register order.
     */
    return KVM_REG_RISCV | KVM_REG_SIZE_U64 | KVM_REG_RISCV_CORE | index;
}

int vart_vcpu_create(VartVcpu *vcpu, VartVm *vm, unsigned long hart_id)
{
    void *mapping;

    memset(vcpu, 0, sizeof(*vcpu));
    vcpu->fd = -1;
    vcpu->vm = vm;
    vcpu->hart_id = hart_id;
    vcpu->run_size = (size_t)vm->kvm->vcpu_mmap_size;

    vcpu->fd = ioctl(vm->fd, KVM_CREATE_VCPU, hart_id);
    if (vcpu->fd < 0) {
        return -errno;
    }

    mapping = mmap(NULL, vcpu->run_size, PROT_READ | PROT_WRITE,
                   MAP_SHARED, vcpu->fd, 0);
    if (mapping == MAP_FAILED) {
        int ret = -errno;

        close(vcpu->fd);
        vcpu->fd = -1;
        return ret;
    }
    vcpu->run = mapping;
    return 0;
}

void vart_vcpu_destroy(VartVcpu *vcpu)
{
    if (vcpu->run != NULL) {
        munmap(vcpu->run, vcpu->run_size);
    }
    if (vcpu->fd >= 0) {
        close(vcpu->fd);
    }
    memset(vcpu, 0, sizeof(*vcpu));
    vcpu->fd = -1;
}

int vart_vcpu_get_one_reg(const VartVcpu *vcpu, uint64_t reg_id, void *value)
{
    struct kvm_one_reg reg = {
        .id = reg_id,
        .addr = (uintptr_t)value,
    };

    if (ioctl(vcpu->fd, KVM_GET_ONE_REG, &reg) < 0) {
        return -errno;
    }
    return 0;
}

int vart_vcpu_set_one_reg(const VartVcpu *vcpu, uint64_t reg_id,
                          const void *value)
{
    struct kvm_one_reg reg = {
        .id = reg_id,
        .addr = (uintptr_t)value,
    };

    if (ioctl(vcpu->fd, KVM_SET_ONE_REG, &reg) < 0) {
        return -errno;
    }
    return 0;
}

int vart_vcpu_get_pc(const VartVcpu *vcpu, uint64_t *value)
{
    return vart_vcpu_get_one_reg(vcpu, RISCV_CORE_REG(regs.pc), value);
}

int vart_vcpu_set_pc(const VartVcpu *vcpu, uint64_t value)
{
    return vart_vcpu_set_one_reg(vcpu, RISCV_CORE_REG(regs.pc), &value);
}

int vart_vcpu_get_gpr(const VartVcpu *vcpu, unsigned int index,
                      uint64_t *value)
{
    if (index > 31 || value == NULL) {
        return -EINVAL;
    }
    if (index == 0) {
        *value = 0;
        return 0;
    }
    return vart_vcpu_get_one_reg(vcpu, riscv_gpr_id(index), value);
}

int vart_vcpu_set_gpr(const VartVcpu *vcpu, unsigned int index,
                      uint64_t value)
{
    if (index > 31) {
        return -EINVAL;
    }
    if (index == 0) {
        return value == 0 ? 0 : -EINVAL;
    }
    return vart_vcpu_set_one_reg(vcpu, riscv_gpr_id(index), &value);
}

int vart_vcpu_run(VartVcpu *vcpu, VartVcpuExit *exit)
{
    int ret;

    memset(exit, 0, sizeof(*exit));
    ret = ioctl(vcpu->fd, KVM_RUN, 0);
    if (ret < 0) {
        if (errno == EINTR || errno == EAGAIN) {
            exit->type = VART_VCPU_EXIT_INTERRUPTED;
            return 0;
        }
        return -errno;
    }

    exit->kvm_reason = vcpu->run->exit_reason;
    switch (vcpu->run->exit_reason) {
    case KVM_EXIT_MMIO:
        exit->type = VART_VCPU_EXIT_MMIO;
        exit->mmio.address = vcpu->run->mmio.phys_addr;
        exit->mmio.size = vcpu->run->mmio.len;
        exit->mmio.is_write = vcpu->run->mmio.is_write != 0;
        memcpy(exit->mmio.data, vcpu->run->mmio.data,
               sizeof(exit->mmio.data));
        break;
    case KVM_EXIT_SYSTEM_EVENT:
        exit->type = VART_VCPU_EXIT_SYSTEM_EVENT;
        exit->system_event.type = vcpu->run->system_event.type;
        exit->system_event.flags = vcpu->run->system_event.flags;
        break;
    case KVM_EXIT_SHUTDOWN:
        exit->type = VART_VCPU_EXIT_SHUTDOWN;
        break;
    default:
        exit->type = VART_VCPU_EXIT_UNKNOWN;
        break;
    }
    return 0;
}
