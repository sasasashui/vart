#include <errno.h>
#include <asm/kvm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vart/kvm.h"
#include "vart/riscv-cpu.h"
#include "vart/vcpu.h"
#include "vart/vm.h"

static int fail(const char *operation, int error)
{
    fprintf(stderr, "not ok - %s: %s\n", operation, strerror(-error));
    return EXIT_FAILURE;
}

int main(void)
{
    const uint64_t test_pc = UINT64_C(0x80200000);
    const uint64_t test_ra = UINT64_C(0x1122334455667788);
    const uint64_t test_sscratch = UINT64_C(0x8877665544332211);
    VartRiscvCpuState *state;
    VartVcpu vcpu;
    VartKvm kvm;
    VartVm vm;
    int ret;

    ret = vart_kvm_open(&kvm);
    if (ret < 0) {
        return fail("open KVM", ret);
    }
    ret = vart_vm_create(&vm, &kvm);
    if (ret < 0) {
        vart_kvm_close(&kvm);
        return fail("create VM", ret);
    }
    ret = vart_vcpu_create(&vcpu, &vm, 0);
    if (ret < 0) {
        vart_vm_destroy(&vm);
        vart_kvm_close(&kvm);
        return fail("create vCPU", ret);
    }
    state = &vcpu.cpu_state;

    ret = vart_riscv_vcpu_get_registers(&vcpu, VART_RISCV_REG_ALL);
    if (ret < 0) {
        goto out;
    }
    if (state->valid != VART_RISCV_REG_ALL || state->dirty != 0 ||
        state->core.gpr[0] != 0 || state->timer.frequency == 0) {
        ret = -EIO;
        goto out;
    }
    if (vart_riscv_cpu_state_mark_dirty(NULL, VART_RISCV_REG_CORE) !=
            -EINVAL ||
        vart_riscv_cpu_state_mark_dirty(state, 0) != -EINVAL ||
        vart_riscv_cpu_state_mark_dirty(state, 1U << 31) != -EINVAL ||
        vart_riscv_vcpu_get_registers(&vcpu, 0) != -EINVAL ||
        vart_riscv_vcpu_put_registers(&vcpu, 1U << 31) != -EINVAL) {
        ret = -EINVAL;
        goto out;
    }
    vcpu.thread_state = VART_VCPU_THREAD_RUNNING;
    if (vart_riscv_vcpu_get_registers(&vcpu, VART_RISCV_REG_CORE) !=
            -EBUSY ||
        vart_riscv_vcpu_put_registers(&vcpu, VART_RISCV_REG_CORE) !=
            -EBUSY) {
        ret = -EINVAL;
        vcpu.thread_state = VART_VCPU_THREAD_CREATED;
        goto out;
    }
    vcpu.thread_state = VART_VCPU_THREAD_CREATED;

    state->core.pc = test_pc;
    state->core.gpr[1] = test_ra;
    state->core.mode = KVM_RISCV_MODE_S;
    state->csr.sscratch = test_sscratch;
    ret = vart_riscv_cpu_state_mark_dirty(state, VART_RISCV_REG_CORE |
                                                  VART_RISCV_REG_CSR |
                                                  VART_RISCV_REG_TIMER);
    if (ret < 0) {
        goto out;
    }
    ret = vart_riscv_vcpu_put_registers(&vcpu, VART_RISCV_REG_ALL);
    if (ret < 0 || state->dirty != 0) {
        ret = ret < 0 ? ret : -EIO;
        goto out;
    }

    memset(&state->core, 0, sizeof(state->core));
    memset(&state->csr, 0, sizeof(state->csr));
    memset(&state->timer, 0, sizeof(state->timer));
    ret = vart_riscv_vcpu_get_registers(&vcpu, VART_RISCV_REG_ALL);
    if (ret < 0 || state->core.pc != test_pc ||
        state->core.gpr[1] != test_ra || state->core.gpr[0] != 0 ||
        state->core.mode != KVM_RISCV_MODE_S ||
        state->csr.sscratch != test_sscratch ||
        state->timer.frequency == 0) {
        ret = ret < 0 ? ret : -EIO;
        goto out;
    }

    ret = vart_vcpu_set_pc(&vcpu, test_pc + 4);
    if (ret < 0 || (state->valid & VART_RISCV_REG_CORE) ||
        (state->dirty & VART_RISCV_REG_CORE)) {
        ret = ret < 0 ? ret : -EIO;
        goto out;
    }
    ret = vart_riscv_vcpu_get_registers(&vcpu, VART_RISCV_REG_CORE);
    if (ret < 0 || state->core.pc != test_pc + 4) {
        ret = ret < 0 ? ret : -EIO;
        goto out;
    }

    state->core.gpr[0] = 1;
    ret = vart_riscv_cpu_state_mark_dirty(state, VART_RISCV_REG_CORE);
    if (ret < 0 ||
        vart_riscv_vcpu_put_registers(&vcpu, VART_RISCV_REG_CORE) !=
            -EINVAL) {
        ret = -EINVAL;
        goto out;
    }
    state->core.gpr[0] = 0;
    state->dirty = 0;
    ret = 0;

out:
    vart_vcpu_destroy(&vcpu);
    vart_vm_destroy(&vm);
    vart_kvm_close(&kvm);
    if (ret < 0) {
        return fail("synchronize RISC-V register state", ret);
    }
    printf("ok - synchronize RISC-V register state\n");
    return EXIT_SUCCESS;
}
