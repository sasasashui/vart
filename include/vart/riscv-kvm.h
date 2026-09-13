#ifndef VART_RISCV_KVM_H
#define VART_RISCV_KVM_H

#include <stdbool.h>

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

int vart_riscv_kvm_probe_vcpu(const VartVcpu *vcpu,
                              VartRiscvKvmCaps *caps);
const char *vart_riscv_kvm_isa_name(VartRiscvKvmIsaFeature feature);
const char *vart_riscv_kvm_sbi_name(VartRiscvKvmSbiFeature feature);

#endif
