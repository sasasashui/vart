#ifndef VART_RISCV_CPU_H
#define VART_RISCV_CPU_H

#include <stdint.h>

struct VartVcpu;

typedef enum VartRiscvRegisterGroup {
    VART_RISCV_REG_CORE = 1U << 0,
    VART_RISCV_REG_CSR = 1U << 1,
    VART_RISCV_REG_TIMER = 1U << 2,
    VART_RISCV_REG_ALL = VART_RISCV_REG_CORE | VART_RISCV_REG_CSR |
                         VART_RISCV_REG_TIMER,
} VartRiscvRegisterGroup;

typedef struct VartRiscvCoreState {
    uint64_t pc;
    uint64_t gpr[32];
    uint64_t mode;
} VartRiscvCoreState;

typedef struct VartRiscvCsrState {
    uint64_t sstatus;
    uint64_t sie;
    uint64_t stvec;
    uint64_t sscratch;
    uint64_t sepc;
    uint64_t scause;
    uint64_t stval;
    uint64_t sip;
    uint64_t satp;
    uint64_t scounteren;
    uint64_t senvcfg;
} VartRiscvCsrState;

typedef struct VartRiscvTimerState {
    uint64_t frequency;
    uint64_t time;
    uint64_t compare;
    uint64_t state;
} VartRiscvTimerState;

typedef struct VartRiscvCpuState {
    VartRiscvCoreState core;
    VartRiscvCsrState csr;
    VartRiscvTimerState timer;
    uint32_t valid;
    uint32_t dirty;
} VartRiscvCpuState;

/* These interfaces serialize on the VM big lock and reject a running vCPU. */
int vart_riscv_vcpu_get_registers(struct VartVcpu *vcpu, uint32_t groups);
int vart_riscv_vcpu_put_registers(struct VartVcpu *vcpu, uint32_t groups);
/* Only state previously populated by GET may become dirty. */
int vart_riscv_cpu_state_mark_dirty(VartRiscvCpuState *state,
                                    uint32_t groups);

#endif
