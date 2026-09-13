#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vart/kvm.h"
#include "vart/riscv-kvm.h"

static void enable_required(VartKvm *kvm, VartRiscvKvmCaps *caps)
{
    static const VartRiscvKvmIsaFeature isa[] = {
        VART_RISCV_KVM_ISA_I,
        VART_RISCV_KVM_ISA_M,
        VART_RISCV_KVM_ISA_A,
    };
    static const VartRiscvKvmSbiFeature sbi[] = {
        VART_RISCV_KVM_SBI_TIME,
        VART_RISCV_KVM_SBI_IPI,
        VART_RISCV_KVM_SBI_RFENCE,
        VART_RISCV_KVM_SBI_SRST,
        VART_RISCV_KVM_SBI_HSM,
    };
    unsigned int i;

    memset(kvm, 0, sizeof(*kvm));
    memset(caps, 0, sizeof(*caps));
    kvm->user_memory = true;
    kvm->one_reg = true;
    kvm->immediate_exit = true;
    kvm->mp_state = true;
    caps->reg_list = true;
    caps->core_mode = true;
    caps->timer_frequency = true;
    for (i = 0; i < sizeof(isa) / sizeof(isa[0]); i++) {
        caps->isa[isa[i]].available = true;
        caps->isa[isa[i]].enabled = true;
    }
    for (i = 0; i < sizeof(sbi) / sizeof(sbi[0]); i++) {
        caps->sbi[sbi[i]].available = true;
        caps->sbi[sbi[i]].enabled = true;
    }
}

int main(void)
{
    VartRiscvBootContractReport report;
    VartRiscvKvmCaps caps;
    VartKvm kvm;

    enable_required(&kvm, &caps);
    memset(&report, 0xff, sizeof(report));
    if (vart_riscv_kvm_validate_boot(&kvm, &caps, &report) != 0 ||
        report.missing_kvm != 0 || report.missing_isa != 0 ||
        report.missing_sbi != 0) {
        goto fail;
    }

    kvm.immediate_exit = false;
    caps.core_mode = false;
    caps.isa[VART_RISCV_KVM_ISA_A].enabled = false;
    caps.sbi[VART_RISCV_KVM_SBI_HSM].available = false;
    if (vart_riscv_kvm_validate_boot(&kvm, &caps, &report) != -ENOTSUP ||
        report.missing_kvm != (VART_RISCV_BOOT_IMMEDIATE_EXIT |
                               VART_RISCV_BOOT_CORE_MODE) ||
        report.missing_isa != (1U << VART_RISCV_KVM_ISA_A) ||
        report.missing_sbi != (1U << VART_RISCV_KVM_SBI_HSM)) {
        goto fail;
    }

    enable_required(&kvm, &caps);
    caps.isa[VART_RISCV_KVM_ISA_SSTC].available = true;
    caps.sbi[VART_RISCV_KVM_SBI_DBCN].available = true;
    if (vart_riscv_kvm_validate_boot(&kvm, &caps, &report) != 0 ||
        vart_riscv_kvm_validate_boot(NULL, &caps, &report) != -EINVAL ||
        vart_riscv_kvm_validate_boot(&kvm, NULL, &report) != -EINVAL ||
        vart_riscv_kvm_validate_boot(&kvm, &caps, NULL) != -EINVAL) {
        goto fail;
    }

    printf("ok - validate the RISC-V KVM boot contract\n");
    return EXIT_SUCCESS;

fail:
    fprintf(stderr, "not ok - validate the RISC-V KVM boot contract\n");
    return EXIT_FAILURE;
}
