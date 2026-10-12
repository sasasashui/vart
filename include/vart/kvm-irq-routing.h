#ifndef VART_KVM_IRQ_ROUTING_H
#define VART_KVM_IRQ_ROUTING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vart/vm.h"

typedef struct VartKvmMsiRoute {
    uint32_t gsi;
    uint64_t address;
    uint32_t data;
    uint32_t devid;
} VartKvmMsiRoute;

typedef struct VartKvmIrqRouting {
    VartVm *vm;
    VartKvmMsiRoute *msi_routes;
    size_t count;
    size_t capacity;
    bool initialized;
} VartKvmIrqRouting;

int vart_kvm_irq_routing_init(VartKvmIrqRouting *routing, VartVm *vm);
void vart_kvm_irq_routing_destroy(VartKvmIrqRouting *routing);
int vart_kvm_irq_routing_clear(VartKvmIrqRouting *routing);
int vart_kvm_irq_routing_add_msi(VartKvmIrqRouting *routing, uint32_t gsi,
                                  uint64_t address, uint32_t data,
                                  uint32_t devid);
int vart_kvm_irq_routing_remove_msi(VartKvmIrqRouting *routing,
                                     uint32_t gsi);
int vart_kvm_irq_routing_commit(VartKvmIrqRouting *routing);

#endif
