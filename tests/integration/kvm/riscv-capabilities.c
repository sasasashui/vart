#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vart/kvm.h"
#include "vart/riscv-kvm.h"
#include "vart/vcpu.h"
#include "vart/vm.h"

static int fail(const char *operation, int error)
{
    fprintf(stderr, "not ok - %s: %s\n", operation, strerror(-error));
    return EXIT_FAILURE;
}

int main(void)
{
    static const VartRiscvKvmIsaFeature required_isa[] = {
        VART_RISCV_KVM_ISA_I,
        VART_RISCV_KVM_ISA_M,
        VART_RISCV_KVM_ISA_A,
    };
    static const VartRiscvKvmSbiFeature required_sbi[] = {
        VART_RISCV_KVM_SBI_TIME,
        VART_RISCV_KVM_SBI_IPI,
        VART_RISCV_KVM_SBI_RFENCE,
        VART_RISCV_KVM_SBI_SRST,
        VART_RISCV_KVM_SBI_HSM,
    };
    VartRiscvKvmCaps caps;
    VartVcpu vcpu;
    VartKvm kvm;
    VartVm vm;
    unsigned int i;
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

    ret = vart_riscv_kvm_probe_vcpu(&vcpu, &caps);
    if (ret < 0) {
        goto out;
    }
    if (vart_riscv_kvm_probe_vcpu(NULL, &caps) != -EINVAL ||
        vart_riscv_kvm_probe_vcpu(&vcpu, NULL) != -EINVAL ||
        vart_riscv_kvm_isa_name(VART_RISCV_KVM_ISA_COUNT) != NULL ||
        vart_riscv_kvm_sbi_name(VART_RISCV_KVM_SBI_COUNT) != NULL) {
        ret = -EINVAL;
        goto out;
    }
    if (!caps.reg_list || !caps.core_mode || !caps.timer_frequency) {
        ret = -ENOTSUP;
        goto out;
    }
    for (i = 0; i < sizeof(required_isa) / sizeof(required_isa[0]); i++) {
        if (!caps.isa[required_isa[i]].available ||
            !caps.isa[required_isa[i]].enabled) {
            ret = -ENOTSUP;
            goto out;
        }
    }
    for (i = 0; i < sizeof(required_sbi) / sizeof(required_sbi[0]); i++) {
        if (!caps.sbi[required_sbi[i]].available ||
            !caps.sbi[required_sbi[i]].enabled) {
            ret = -ENOTSUP;
            goto out;
        }
    }

out:
    vart_vcpu_destroy(&vcpu);
    vart_vm_destroy(&vm);
    vart_kvm_close(&kvm);
    if (ret < 0) {
        return fail("probe RISC-V boot capabilities", ret);
    }
    printf("ok - probe RISC-V KVM boot capabilities\n");
    return EXIT_SUCCESS;
}
