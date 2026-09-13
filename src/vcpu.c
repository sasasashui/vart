#define _GNU_SOURCE

#include <errno.h>
#include <linux/kvm.h>
#include <asm/kvm.h>
#include <stdint.h>
#include <signal.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "vart/vcpu.h"

#define RISCV_CORE_REG(name) \
    (KVM_REG_RISCV | KVM_REG_SIZE_U64 | KVM_REG_RISCV_CORE | \
     KVM_REG_RISCV_CORE_REG(name))

static pthread_once_t kick_signal_once = PTHREAD_ONCE_INIT;
static int kick_signal_error;
static _Thread_local VartVcpu *current_vcpu;

static void vart_vcpu_kick_handler(int signal)
{
    (void)signal;
    if (current_vcpu != NULL) {
        current_vcpu->run->immediate_exit = 1;
    }
}

static void vart_vcpu_install_kick_signal(void)
{
    struct sigaction action = {
        .sa_handler = vart_vcpu_kick_handler,
    };

    sigemptyset(&action.sa_mask);
    if (sigaction(VART_VCPU_KICK_SIGNAL, &action, NULL) < 0) {
        kick_signal_error = errno;
    }
}

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
    sigset_t signals;
    int action;
    int ret;

    current_vcpu = vcpu;
    sigemptyset(&signals);
    sigaddset(&signals, VART_VCPU_KICK_SIGNAL);
    ret = pthread_sigmask(SIG_UNBLOCK, &signals, NULL);
    if (ret != 0) {
        vart_mutex_lock(&vcpu->vm->big_lock);
        vcpu->thread_result = -ret;
        vcpu->thread_state = VART_VCPU_THREAD_STOPPED;
        vart_mutex_unlock(&vcpu->vm->big_lock);
        current_vcpu = NULL;
        return NULL;
    }

    for (;;) {
        if (atomic_load_explicit(&vcpu->stop_requested,
                                 memory_order_acquire)) {
            action = 1;
        } else if (atomic_exchange_explicit(&vcpu->kick_requested, false,
                                            memory_order_acq_rel)) {
            memset(&exit, 0, sizeof(exit));
            exit.type = VART_VCPU_EXIT_INTERRUPTED;
            action = 0;
        } else {
            action = vart_vcpu_run(vcpu, &exit);
            vcpu->run->immediate_exit = 0;
            if (action == 0 && exit.type == VART_VCPU_EXIT_INTERRUPTED) {
                atomic_store_explicit(&vcpu->kick_requested, false,
                                      memory_order_release);
            }
        }

        vart_mutex_lock(&vcpu->vm->big_lock);
        if (atomic_load_explicit(&vcpu->stop_requested,
                                 memory_order_acquire)) {
            action = 1;
        } else if (action == 0) {
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
    current_vcpu = NULL;
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
    atomic_init(&vcpu->kick_requested, false);
    atomic_init(&vcpu->stop_requested, false);
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

    vart_mutex_lock(&vm->big_lock);
    vcpu->next = vm->vcpus;
    vm->vcpus = vcpu;
    vart_mutex_unlock(&vm->big_lock);
    return 0;
}

void vart_vcpu_destroy(VartVcpu *vcpu)
{
    if (vcpu->thread_created && !vcpu->thread_joined) {
        pthread_join(vcpu->thread, NULL);
    }
    if (vcpu->vm != NULL) {
        VartVcpu **link;

        vart_mutex_lock(&vcpu->vm->big_lock);
        for (link = &vcpu->vm->vcpus; *link != NULL;
             link = &(*link)->next) {
            if (*link == vcpu) {
                *link = vcpu->next;
                break;
            }
        }
        vart_mutex_unlock(&vcpu->vm->big_lock);
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
    if (!vcpu->vm->kvm->immediate_exit) {
        return -ENOTSUP;
    }
    ret = vart_thread_block_signal(VART_VCPU_KICK_SIGNAL);
    if (ret < 0) {
        return ret;
    }
    ret = pthread_once(&kick_signal_once, vart_vcpu_install_kick_signal);
    if (ret != 0) {
        return -ret;
    }
    if (kick_signal_error != 0) {
        return -kick_signal_error;
    }

    vart_mutex_lock(&vcpu->vm->big_lock);
    if (vcpu->thread_state != VART_VCPU_THREAD_CREATED ||
        vcpu->thread_created || vcpu->vm->shutdown_requested) {
        vart_mutex_unlock(&vcpu->vm->big_lock);
        return -EINVAL;
    }
    vcpu->exit_handler = handler;
    vcpu->exit_opaque = opaque;
    vcpu->thread_result = 0;
    vcpu->thread_state = VART_VCPU_THREAD_RUNNING;
    ret = vart_thread_create(&vcpu->thread, vart_vcpu_thread, vcpu);
    if (ret < 0) {
        vcpu->thread_state = VART_VCPU_THREAD_CREATED;
        vart_mutex_unlock(&vcpu->vm->big_lock);
        return ret;
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

static int vart_vcpu_kick_locked(VartVcpu *vcpu, bool stop)
{
    int ret;

    vart_mutex_assert_held(&vcpu->vm->big_lock);
    if (vcpu->thread_state != VART_VCPU_THREAD_RUNNING) {
        return -EINVAL;
    }
    if (stop) {
        atomic_store_explicit(&vcpu->stop_requested, true,
                              memory_order_release);
    } else {
        atomic_store_explicit(&vcpu->kick_requested, true,
                              memory_order_release);
    }
    ret = pthread_kill(vcpu->thread, VART_VCPU_KICK_SIGNAL);
    return ret == 0 ? 0 : -ret;
}

int vart_vcpu_kick(VartVcpu *vcpu)
{
    int ret;

    if (vcpu == NULL) {
        return -EINVAL;
    }
    vart_mutex_lock(&vcpu->vm->big_lock);
    ret = vart_vcpu_kick_locked(vcpu, false);
    vart_mutex_unlock(&vcpu->vm->big_lock);
    return ret;
}

int vart_vcpu_request_stop(VartVcpu *vcpu)
{
    int ret;

    if (vcpu == NULL) {
        return -EINVAL;
    }
    vart_mutex_lock(&vcpu->vm->big_lock);
    ret = vart_vcpu_kick_locked(vcpu, true);
    vart_mutex_unlock(&vcpu->vm->big_lock);
    return ret;
}

int vart_vm_request_shutdown(VartVm *vm)
{
    VartVcpu *vcpu;
    int result = 0;
    int ret;

    if (vm == NULL) {
        return -EINVAL;
    }

    vart_mutex_lock(&vm->big_lock);
    vm->shutdown_requested = true;
    for (vcpu = vm->vcpus; vcpu != NULL; vcpu = vcpu->next) {
        if (vcpu->thread_state != VART_VCPU_THREAD_RUNNING) {
            continue;
        }
        ret = vart_vcpu_kick_locked(vcpu, true);
        if (ret < 0 && result == 0) {
            result = ret;
        }
    }
    vart_mutex_unlock(&vm->big_lock);
    return result;
}
