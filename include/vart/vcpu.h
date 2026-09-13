#ifndef VART_VCPU_H
#define VART_VCPU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <linux/kvm.h>

#include "vart/vm.h"

typedef struct VartVcpu {
    VartVm *vm;
    int fd;
    unsigned long hart_id;
    struct kvm_run *run;
    size_t run_size;
} VartVcpu;

typedef enum VartVcpuExitType {
    VART_VCPU_EXIT_MMIO,
    VART_VCPU_EXIT_SYSTEM_EVENT,
    VART_VCPU_EXIT_SHUTDOWN,
    VART_VCPU_EXIT_INTERRUPTED,
    VART_VCPU_EXIT_UNKNOWN,
} VartVcpuExitType;

typedef struct VartVcpuExit {
    VartVcpuExitType type;
    unsigned int kvm_reason;
    union {
        struct {
            uint64_t address;
            uint8_t data[8];
            uint32_t size;
            bool is_write;
        } mmio;
        struct {
            uint32_t type;
            uint64_t flags;
        } system_event;
    };
} VartVcpuExit;

int vart_vcpu_create(VartVcpu *vcpu, VartVm *vm, unsigned long hart_id);
void vart_vcpu_destroy(VartVcpu *vcpu);

int vart_vcpu_get_one_reg(const VartVcpu *vcpu, uint64_t reg_id,
                          void *value);
int vart_vcpu_set_one_reg(const VartVcpu *vcpu, uint64_t reg_id,
                          const void *value);
int vart_vcpu_get_pc(const VartVcpu *vcpu, uint64_t *value);
int vart_vcpu_set_pc(const VartVcpu *vcpu, uint64_t value);
int vart_vcpu_get_gpr(const VartVcpu *vcpu, unsigned int index,
                      uint64_t *value);
int vart_vcpu_set_gpr(const VartVcpu *vcpu, unsigned int index,
                      uint64_t value);
int vart_vcpu_run(VartVcpu *vcpu, VartVcpuExit *exit);

#endif
