#ifndef VART_RISCV_KVM_H
#define VART_RISCV_KVM_H

#include <stdbool.h>
#include <stdint.h>

#include "vart/vcpu.h"

typedef struct VartRiscvKvmFeature {
    bool available;
    bool enabled;
} VartRiscvKvmFeature;

typedef enum VartRiscvKvmIsaFeature {
    VART_RISCV_KVM_ISA_I,
    VART_RISCV_KVM_ISA_M,
    VART_RISCV_KVM_ISA_A,
    VART_RISCV_KVM_ISA_SSTC,
    VART_RISCV_KVM_ISA_SSAIA,
    VART_RISCV_KVM_ISA_COUNT,
} VartRiscvKvmIsaFeature;

typedef enum VartRiscvKvmSbiFeature {
    VART_RISCV_KVM_SBI_V01,
    VART_RISCV_KVM_SBI_TIME,
    VART_RISCV_KVM_SBI_IPI,
    VART_RISCV_KVM_SBI_RFENCE,
    VART_RISCV_KVM_SBI_SRST,
    VART_RISCV_KVM_SBI_HSM,
    VART_RISCV_KVM_SBI_DBCN,
    VART_RISCV_KVM_SBI_COUNT,
} VartRiscvKvmSbiFeature;

typedef struct VartRiscvKvmCaps {
    bool reg_list;
    bool core_mode;
    bool timer_frequency;
    VartRiscvKvmFeature isa[VART_RISCV_KVM_ISA_COUNT];
    VartRiscvKvmFeature sbi[VART_RISCV_KVM_SBI_COUNT];
} VartRiscvKvmCaps;

typedef enum VartRiscvBootRequirement {
    VART_RISCV_BOOT_USER_MEMORY = 1U << 0,
    VART_RISCV_BOOT_ONE_REG = 1U << 1,
    VART_RISCV_BOOT_IMMEDIATE_EXIT = 1U << 2,
    VART_RISCV_BOOT_MP_STATE = 1U << 3,
    VART_RISCV_BOOT_REG_LIST = 1U << 4,
    VART_RISCV_BOOT_CORE_MODE = 1U << 5,
    VART_RISCV_BOOT_TIMER_FREQUENCY = 1U << 6,
} VartRiscvBootRequirement;

typedef struct VartRiscvBootContractReport {
    uint32_t missing_kvm;
    uint32_t missing_isa;
    uint32_t missing_sbi;
} VartRiscvBootContractReport;

int vart_riscv_kvm_probe_vcpu(const VartVcpu *vcpu,
                              VartRiscvKvmCaps *caps);
const char *vart_riscv_kvm_isa_name(VartRiscvKvmIsaFeature feature);
const char *vart_riscv_kvm_sbi_name(VartRiscvKvmSbiFeature feature);
int vart_riscv_kvm_validate_boot(const VartKvm *kvm,
                                 const VartRiscvKvmCaps *caps,
                                 VartRiscvBootContractReport *report);

#endif
