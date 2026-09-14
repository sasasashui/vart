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
    uint32_t nr_sources;
    size_t vcpu_count;
    uint64_t imsic_base;
    uint64_t aplic_base;
    bool initialized;
} VartRiscvAia;

int vart_riscv_aia_create(VartRiscvAia *aia, VartVm *vm, uint32_t mode);
int vart_riscv_aia_init_imsic(VartRiscvAia *aia, size_t vcpu_count,
                              uint64_t imsic_base, uint32_t nr_ids);
int vart_riscv_aia_init_aplic(VartRiscvAia *aia, size_t vcpu_count,
                              uint64_t imsic_base, uint32_t nr_ids,
                              uint64_t aplic_base, uint32_t nr_sources);
int vart_riscv_aia_signal_msi(VartRiscvAia *aia, size_t vcpu_index,
                              uint32_t interrupt_id);
int vart_riscv_aia_set_irq(VartRiscvAia *aia, uint32_t irq, bool level);
int vart_riscv_aia_pulse_irq(VartRiscvAia *aia, uint32_t irq);
void vart_riscv_aia_destroy(VartRiscvAia *aia);

#endif
