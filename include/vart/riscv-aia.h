#ifndef VART_RISCV_AIA_H
#define VART_RISCV_AIA_H

#include <stdint.h>

#include "vart/kvm-device.h"

typedef struct VartRiscvAia {
    VartKvmDevice device;
    uint32_t mode;
} VartRiscvAia;

int vart_riscv_aia_create(VartRiscvAia *aia, VartVm *vm, uint32_t mode);
void vart_riscv_aia_destroy(VartRiscvAia *aia);

#endif
