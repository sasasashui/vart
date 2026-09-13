#ifndef VART_RISCV_SBI_H
#define VART_RISCV_SBI_H

#include <stdint.h>

#include "vart/vcpu.h"

#define VART_SBI_ERR_NOT_SUPPORTED (-2L)

typedef struct VartRiscvSbiResponse {
    long error;
    uint64_t value;
} VartRiscvSbiResponse;

typedef int (*VartRiscvSbiCallback)(const VartVcpuExit *exit,
                                    VartRiscvSbiResponse *response,
                                    void *opaque);

typedef struct VartRiscvSbiHandler {
    uint64_t extension_start;
    uint64_t extension_end;
    VartRiscvSbiCallback callback;
    void *opaque;
    struct VartRiscvSbiHandler *next;
} VartRiscvSbiHandler;

typedef struct VartRiscvSbiDispatcher {
    VartRiscvSbiHandler *handlers;
} VartRiscvSbiDispatcher;

void vart_riscv_sbi_dispatcher_init(VartRiscvSbiDispatcher *dispatcher);
void vart_riscv_sbi_handler_init(VartRiscvSbiHandler *handler,
                                 uint64_t extension_start,
                                 uint64_t extension_end,
                                 VartRiscvSbiCallback callback,
                                 void *opaque);
int vart_riscv_sbi_add_handler(VartRiscvSbiDispatcher *dispatcher,
                               VartRiscvSbiHandler *handler);
int vart_riscv_sbi_dispatch(VartRiscvSbiDispatcher *dispatcher,
                            VartVcpu *vcpu, const VartVcpuExit *exit);

#endif
