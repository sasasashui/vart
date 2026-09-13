#ifndef VART_MEMORY_H
#define VART_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "vart/vm.h"

typedef struct VartMemoryRegion {
    void *host_addr;
    uint64_t guest_addr;
    size_t size;
    unsigned int slot;
    bool registered;
} VartMemoryRegion;

int vart_memory_region_create(VartMemoryRegion *region, uint64_t guest_addr,
                              size_t size, unsigned int slot);
int vart_memory_region_register(VartMemoryRegion *region, const VartVm *vm);
int vart_memory_region_unregister(VartMemoryRegion *region, const VartVm *vm);
int vart_memory_region_write(VartMemoryRegion *region, uint64_t guest_addr,
                             const void *data, size_t size);
void vart_memory_region_destroy(VartMemoryRegion *region);

#endif
