#include <errno.h>
#include <asm/kvm.h>
#include <linux/kvm.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vart/riscv-cpu.h"
#include "vart/vcpu.h"

#define RISCV_REG(type, index) \
    (KVM_REG_RISCV | KVM_REG_SIZE_U64 | (type) | (index))
#define CORE_REG(name) \
    RISCV_REG(KVM_REG_RISCV_CORE, KVM_REG_RISCV_CORE_REG(name))
#define CSR_REG(name) \
    RISCV_REG(KVM_REG_RISCV_CSR, KVM_REG_RISCV_CSR_REG(name))
#define TIMER_REG(name) \
    RISCV_REG(KVM_REG_RISCV_TIMER, KVM_REG_RISCV_TIMER_REG(name))

typedef struct VartRiscvRegister {
    uint64_t id;
    size_t offset;
    bool writable;
} VartRiscvRegister;

#define STATE_REG(id, member) \
    { (id), offsetof(VartRiscvCpuState, member), true }
#define READ_ONLY_STATE_REG(id, member) \
    { (id), offsetof(VartRiscvCpuState, member), false }

static const VartRiscvRegister csr_registers[] = {
    STATE_REG(CSR_REG(sstatus), csr.sstatus),
    STATE_REG(CSR_REG(sie), csr.sie),
    STATE_REG(CSR_REG(stvec), csr.stvec),
    STATE_REG(CSR_REG(sscratch), csr.sscratch),
    STATE_REG(CSR_REG(sepc), csr.sepc),
    STATE_REG(CSR_REG(scause), csr.scause),
    STATE_REG(CSR_REG(stval), csr.stval),
    STATE_REG(CSR_REG(sip), csr.sip),
    STATE_REG(CSR_REG(satp), csr.satp),
    STATE_REG(CSR_REG(scounteren), csr.scounteren),
    STATE_REG(CSR_REG(senvcfg), csr.senvcfg),
};

static const VartRiscvRegister timer_registers[] = {
    READ_ONLY_STATE_REG(TIMER_REG(frequency), timer.frequency),
    STATE_REG(TIMER_REG(time), timer.time),
    STATE_REG(TIMER_REG(compare), timer.compare),
    STATE_REG(TIMER_REG(state), timer.state),
};

static int get_register_array(VartVcpu *vcpu,
                              const VartRiscvRegister *registers,
                              size_t count)
{
    size_t i;

    for (i = 0; i < count; i++) {
        uint64_t *value = (uint64_t *)((char *)&vcpu->cpu_state +
                                      registers[i].offset);
        int ret;

        ret = vart_vcpu_get_one_reg(vcpu, registers[i].id, value);
        if (ret < 0) {
            return ret;
        }
    }
    return 0;
}

static int put_register_array(VartVcpu *vcpu,
                              const VartRiscvRegister *registers,
                              size_t count)
{
    size_t i;

    for (i = 0; i < count; i++) {
        const uint64_t *value = (const uint64_t *)((const char *)
                                &vcpu->cpu_state + registers[i].offset);
        int ret;

        if (!registers[i].writable) {
            continue;
        }
        /* KVM currently rejects writing an inactive timer state. */
        if (registers[i].id == TIMER_REG(state) && *value == 0) {
            continue;
        }
        ret = vart_vcpu_set_one_reg(vcpu, registers[i].id, value);
        if (ret < 0) {
            return ret;
        }
    }
    return 0;
}

static int get_core(VartVcpu *vcpu)
{
    unsigned int i;
    int ret;

    ret = vart_vcpu_get_one_reg(vcpu, CORE_REG(regs.pc),
                                &vcpu->cpu_state.core.pc);
    if (ret < 0) {
        return ret;
    }
    vcpu->cpu_state.core.gpr[0] = 0;
    for (i = 1; i < 32; i++) {
        ret = vart_vcpu_get_one_reg(vcpu,
            RISCV_REG(KVM_REG_RISCV_CORE, i),
            &vcpu->cpu_state.core.gpr[i]);
        if (ret < 0) {
            return ret;
        }
    }
    return vart_vcpu_get_one_reg(vcpu, CORE_REG(mode),
                                 &vcpu->cpu_state.core.mode);
}

static int put_core(VartVcpu *vcpu)
{
    unsigned int i;
    int ret;

    ret = vart_vcpu_set_one_reg(vcpu, CORE_REG(regs.pc),
                                &vcpu->cpu_state.core.pc);
    if (ret < 0) {
        return ret;
    }
    for (i = 1; i < 32; i++) {
        ret = vart_vcpu_set_one_reg(vcpu,
            RISCV_REG(KVM_REG_RISCV_CORE, i),
            &vcpu->cpu_state.core.gpr[i]);
        if (ret < 0) {
            return ret;
        }
    }
    return vart_vcpu_set_one_reg(vcpu, CORE_REG(mode),
                                 &vcpu->cpu_state.core.mode);
}

static int validate_sync(VartVcpu *vcpu, uint32_t groups)
{
    if (vcpu == NULL || groups == 0 || (groups & ~VART_RISCV_REG_ALL)) {
        return -EINVAL;
    }
    if (vcpu->thread_state == VART_VCPU_THREAD_RUNNING) {
        return -EBUSY;
    }
    return 0;
}

int vart_riscv_vcpu_get_registers(VartVcpu *vcpu, uint32_t groups)
{
    int ret;

    if (vcpu == NULL) {
        return -EINVAL;
    }
    vart_mutex_lock(&vcpu->vm->big_lock);
    ret = validate_sync(vcpu, groups);
    if (ret < 0) {
        goto out;
    }
    if (groups & VART_RISCV_REG_CORE) {
        ret = get_core(vcpu);
        if (ret < 0) {
            goto out;
        }
        vcpu->cpu_state.valid |= VART_RISCV_REG_CORE;
        vcpu->cpu_state.dirty &= ~VART_RISCV_REG_CORE;
    }
    if (groups & VART_RISCV_REG_CSR) {
        ret = get_register_array(vcpu, csr_registers,
                                 sizeof(csr_registers) /
                                 sizeof(csr_registers[0]));
        if (ret < 0) {
            goto out;
        }
        vcpu->cpu_state.valid |= VART_RISCV_REG_CSR;
        vcpu->cpu_state.dirty &= ~VART_RISCV_REG_CSR;
    }
    if (groups & VART_RISCV_REG_TIMER) {
        ret = get_register_array(vcpu, timer_registers,
                                 sizeof(timer_registers) /
                                 sizeof(timer_registers[0]));
        if (ret < 0) {
            goto out;
        }
        vcpu->cpu_state.valid |= VART_RISCV_REG_TIMER;
        vcpu->cpu_state.dirty &= ~VART_RISCV_REG_TIMER;
    }
out:
    vart_mutex_unlock(&vcpu->vm->big_lock);
    return ret;
}

int vart_riscv_vcpu_put_registers(VartVcpu *vcpu, uint32_t groups)
{
    uint32_t pending;
    int ret;

    if (vcpu == NULL) {
        return -EINVAL;
    }
    vart_mutex_lock(&vcpu->vm->big_lock);
    ret = validate_sync(vcpu, groups);
    if (ret < 0) {
        goto out;
    }
    pending = groups & vcpu->cpu_state.dirty;
    if (pending & ~vcpu->cpu_state.valid) {
        ret = -EINVAL;
        goto out;
    }
    if (pending & VART_RISCV_REG_CORE) {
        if (vcpu->cpu_state.core.gpr[0] != 0 ||
            (vcpu->cpu_state.core.mode != KVM_RISCV_MODE_S &&
             vcpu->cpu_state.core.mode != KVM_RISCV_MODE_U)) {
            ret = -EINVAL;
            goto out;
        }
        ret = put_core(vcpu);
        if (ret < 0) {
            goto out;
        }
        vcpu->cpu_state.dirty &= ~VART_RISCV_REG_CORE;
    }
    if (pending & VART_RISCV_REG_CSR) {
        ret = put_register_array(vcpu, csr_registers,
                                 sizeof(csr_registers) /
                                 sizeof(csr_registers[0]));
        if (ret < 0) {
            goto out;
        }
        vcpu->cpu_state.dirty &= ~VART_RISCV_REG_CSR;
    }
    if (pending & VART_RISCV_REG_TIMER) {
        ret = put_register_array(vcpu, timer_registers,
                                 sizeof(timer_registers) /
                                 sizeof(timer_registers[0]));
        if (ret < 0) {
            goto out;
        }
        vcpu->cpu_state.dirty &= ~VART_RISCV_REG_TIMER;
    }
    ret = 0;
out:
    vart_mutex_unlock(&vcpu->vm->big_lock);
    return ret;
}

int vart_riscv_cpu_state_mark_dirty(VartRiscvCpuState *state,
                                    uint32_t groups)
{
    if (state == NULL || groups == 0 || (groups & ~VART_RISCV_REG_ALL) ||
        (groups & ~state->valid)) {
        return -EINVAL;
    }
    state->dirty |= groups;
    return 0;
}
