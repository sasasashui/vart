#include <errno.h>
#include <linux/kvm.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "vart/kvm-irq-routing.h"

static int vart_kvm_irq_routing_reserve(VartKvmIrqRouting *routing)
{
    VartKvmMsiRoute *routes;
    size_t capacity;

    if (routing->count < routing->capacity) {
        return 0;
    }
    capacity = routing->capacity == 0 ? 8 : routing->capacity * 2;
    if (capacity < routing->capacity ||
        capacity > SIZE_MAX / sizeof(*routes)) {
        return -EOVERFLOW;
    }
    routes = realloc(routing->msi_routes, capacity * sizeof(*routes));
    if (routes == NULL) {
        return -ENOMEM;
    }
    routing->msi_routes = routes;
    routing->capacity = capacity;
    return 0;
}

int vart_kvm_irq_routing_init(VartKvmIrqRouting *routing, VartVm *vm)
{
    if (routing == NULL || vm == NULL || vm->fd < 0 || vm->kvm == NULL) {
        return -EINVAL;
    }
    if (routing->initialized) {
        return -EBUSY;
    }
    memset(routing, 0, sizeof(*routing));
    if (!vm->kvm->irq_routing || !vm->kvm->signal_msi) {
        return -ENOTSUP;
    }
    routing->vm = vm;
    routing->initialized = true;
    return 0;
}

void vart_kvm_irq_routing_destroy(VartKvmIrqRouting *routing)
{
    if (routing == NULL) {
        return;
    }
    if (routing->initialized) {
        free(routing->msi_routes);
    }
    memset(routing, 0, sizeof(*routing));
}

int vart_kvm_irq_routing_clear(VartKvmIrqRouting *routing)
{
    if (routing == NULL || !routing->initialized) {
        return -EINVAL;
    }
    routing->count = 0;
    return 0;
}

int vart_kvm_irq_routing_add_msi(VartKvmIrqRouting *routing, uint32_t gsi,
                                 uint64_t address, uint32_t data,
                                 uint32_t devid)
{
    int ret;

    if (routing == NULL || !routing->initialized) {
        return -EINVAL;
    }
    ret = vart_kvm_irq_routing_reserve(routing);
    if (ret < 0) {
        return ret;
    }
    routing->msi_routes[routing->count++] = (VartKvmMsiRoute) {
        .gsi = gsi,
        .address = address,
        .data = data,
        .devid = devid,
    };
    return 0;
}

int vart_kvm_irq_routing_commit(VartKvmIrqRouting *routing)
{
    struct kvm_irq_routing *table;
    size_t size;
    size_t i;
    int ret;

    if (routing == NULL || !routing->initialized || routing->vm == NULL) {
        return -EINVAL;
    }
    if (routing->count > (SIZE_MAX - sizeof(*table)) /
                         sizeof(table->entries[0]) ||
        routing->count > UINT32_MAX) {
        return -EOVERFLOW;
    }
    size = sizeof(*table) + routing->count * sizeof(table->entries[0]);
    table = calloc(1, size);
    if (table == NULL) {
        return -ENOMEM;
    }
    table->nr = (uint32_t)routing->count;
    for (i = 0; i < routing->count; i++) {
        const VartKvmMsiRoute *route = &routing->msi_routes[i];
        struct kvm_irq_routing_entry *entry = &table->entries[i];

        entry->gsi = route->gsi;
        entry->type = KVM_IRQ_ROUTING_MSI;
        entry->u.msi.address_lo = (uint32_t)route->address;
        entry->u.msi.address_hi = (uint32_t)(route->address >> 32);
        entry->u.msi.data = route->data;
        entry->u.msi.devid = route->devid;
    }
    ret = vart_vm_ioctl(routing->vm, KVM_SET_GSI_ROUTING,
                        (unsigned long)(uintptr_t)table);
    if (ret < 0) {
        ret = -errno;
    } else {
        ret = 0;
    }
    free(table);
    return ret;
}
