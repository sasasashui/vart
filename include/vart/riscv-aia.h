#ifndef VART_RISCV_AIA_H
#define VART_RISCV_AIA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vart/kvm-device.h"

typedef struct VartRiscvAia {
    VartKvmDevice device;
    uint32_t mode;
    uint32_t nr_ids;
    size_t vcpu_count;
    uint64_t imsic_base;
    bool initialized;
} VartRiscvAia;

int vart_riscv_aia_create(VartRiscvAia *aia, VartVm *vm, uint32_t mode);
int vart_riscv_aia_init_imsic(VartRiscvAia *aia, size_t vcpu_count,
                              uint64_t imsic_base, uint32_t nr_ids);
int vart_riscv_aia_signal_msi(VartRiscvAia *aia, size_t vcpu_index,
                              uint32_t interrupt_id);
void vart_riscv_aia_destroy(VartRiscvAia *aia);

#endif
