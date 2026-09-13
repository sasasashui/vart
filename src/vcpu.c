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

static void *vart_vcpu_thread(void *opaque)
{
    VartVcpu *vcpu = opaque;
    VartVcpuExit exit;
    int action;

    for (;;) {
        action = vart_vcpu_run(vcpu, &exit);

        vart_mutex_lock(&vcpu->vm->big_lock);
        if (action == 0) {
            action = vcpu->exit_handler(vcpu, &exit, vcpu->exit_opaque);
        }
        if (action != 0) {
            vcpu->thread_result = action < 0 ? action : 0;
            vcpu->thread_state = VART_VCPU_THREAD_STOPPED;
            vart_mutex_unlock(&vcpu->vm->big_lock);
            break;
        }
        vart_mutex_unlock(&vcpu->vm->big_lock);
    }
    return NULL;
}

int vart_vcpu_create(VartVcpu *vcpu, VartVm *vm, unsigned long hart_id)
{
    void *mapping;

    memset(vcpu, 0, sizeof(*vcpu));
    vcpu->fd = -1;
    vcpu->vm = vm;
    vcpu->hart_id = hart_id;
    vcpu->thread_state = VART_VCPU_THREAD_CREATED;
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
    if (vcpu->thread_created && !vcpu->thread_joined) {
        pthread_join(vcpu->thread, NULL);
    }
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

int vart_vcpu_set_mode(const VartVcpu *vcpu, unsigned long mode)
{
    if (mode != KVM_RISCV_MODE_S && mode != KVM_RISCV_MODE_U) {
        return -EINVAL;
    }
    return vart_vcpu_set_one_reg(vcpu, RISCV_CORE_REG(mode), &mode);
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
        vcpu->mmio_read_pending = !exit->mmio.is_write;
        vcpu->mmio_read_size = exit->mmio.size;
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

int vart_vcpu_complete_mmio_read(VartVcpu *vcpu, uint64_t value)
{
    unsigned int i;

    if (!vcpu->mmio_read_pending ||
        (vcpu->mmio_read_size != 1 && vcpu->mmio_read_size != 2 &&
         vcpu->mmio_read_size != 4 && vcpu->mmio_read_size != 8)) {
        return -EINVAL;
    }
    for (i = 0; i < vcpu->mmio_read_size; i++) {
        vcpu->run->mmio.data[i] = value >> (i * 8);
    }
    vcpu->mmio_read_pending = false;
    return 0;
}

int vart_vcpu_start(VartVcpu *vcpu, VartVcpuExitHandler handler,
                    void *opaque)
{
    int ret;

    if (vcpu == NULL || handler == NULL) {
        return -EINVAL;
    }

    vart_mutex_lock(&vcpu->vm->big_lock);
    if (vcpu->thread_state != VART_VCPU_THREAD_CREATED ||
        vcpu->thread_created) {
        vart_mutex_unlock(&vcpu->vm->big_lock);
        return -EINVAL;
    }
    vcpu->exit_handler = handler;
    vcpu->exit_opaque = opaque;
    vcpu->thread_result = 0;
    vcpu->thread_state = VART_VCPU_THREAD_RUNNING;
    ret = pthread_create(&vcpu->thread, NULL, vart_vcpu_thread, vcpu);
    if (ret != 0) {
        vcpu->thread_state = VART_VCPU_THREAD_CREATED;
        vart_mutex_unlock(&vcpu->vm->big_lock);
        return -ret;
    }
    vcpu->thread_created = true;
    vart_mutex_unlock(&vcpu->vm->big_lock);
    return 0;
}

int vart_vcpu_join(VartVcpu *vcpu)
{
    int result;
    int ret;

    if (vcpu == NULL) {
        return -EINVAL;
    }

    vart_mutex_lock(&vcpu->vm->big_lock);
    if (!vcpu->thread_created || vcpu->thread_joined) {
        vart_mutex_unlock(&vcpu->vm->big_lock);
        return -EINVAL;
    }
    vart_mutex_unlock(&vcpu->vm->big_lock);

    ret = pthread_join(vcpu->thread, NULL);
    if (ret != 0) {
        return -ret;
    }

    vart_mutex_lock(&vcpu->vm->big_lock);
    vcpu->thread_joined = true;
    result = vcpu->thread_result;
    vart_mutex_unlock(&vcpu->vm->big_lock);
    return result;
}

VartVcpuThreadState vart_vcpu_thread_state(VartVcpu *vcpu)
{
    VartVcpuThreadState state;

    vart_mutex_lock(&vcpu->vm->big_lock);
    state = vcpu->thread_state;
    vart_mutex_unlock(&vcpu->vm->big_lock);
    return state;
}
