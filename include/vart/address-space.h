#ifndef VART_ADDRESS_SPACE_H
#define VART_ADDRESS_SPACE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct VartAddressSpace VartAddressSpace;

typedef struct VartMmioOps {
    int (*read)(void *opaque, uint64_t offset, unsigned int size,
                uint64_t *value);
    int (*write)(void *opaque, uint64_t offset, unsigned int size,
                 uint64_t value);
} VartMmioOps;

typedef enum VartAddressRegionType {
    VART_REGION_RAM,
    VART_REGION_ROM,
    VART_REGION_MMIO,
    VART_REGION_ALIAS,
} VartAddressRegionType;

typedef struct VartAddressRegion {
    uint64_t base;
    uint64_t size;
    int priority;
    VartAddressRegionType type;
    bool enabled;
    void *owner;
    VartAddressSpace *address_space;
    const VartMmioOps *mmio_ops;
    void *mmio_opaque;
} VartAddressRegion;

struct VartAddressSpace {
    VartAddressRegion **regions;
    size_t count;
    size_t capacity;
};

int vart_address_region_init(VartAddressRegion *region,
                             VartAddressRegionType type, uint64_t base,
                             uint64_t size, int priority, void *owner);
int vart_address_region_init_mmio(VartAddressRegion *region, uint64_t base,
                                  uint64_t size, int priority, void *owner,
                                  const VartMmioOps *ops, void *opaque);
int vart_address_region_set_enabled(VartAddressRegion *region, bool enabled);
bool vart_address_region_contains(const VartAddressRegion *region,
                                  uint64_t address, uint64_t size);
void vart_address_space_init(VartAddressSpace *address_space);
void vart_address_space_destroy(VartAddressSpace *address_space);
int vart_address_space_add(VartAddressSpace *address_space,
                           VartAddressRegion *region);
int vart_address_space_remove(VartAddressSpace *address_space,
                              VartAddressRegion *region);
int vart_address_space_set_enabled(VartAddressSpace *address_space,
                                   VartAddressRegion *region, bool enabled);
VartAddressRegion *vart_address_space_find(const VartAddressSpace *address_space,
                                           uint64_t address, uint64_t size);
int vart_address_space_read(const VartAddressSpace *address_space,
                            uint64_t address, unsigned int size,
                            uint64_t *value);
int vart_address_space_write(const VartAddressSpace *address_space,
                             uint64_t address, unsigned int size,
                             uint64_t value);

#endif
