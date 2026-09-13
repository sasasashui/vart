#ifndef VART_ADDRESS_SPACE_H
#define VART_ADDRESS_SPACE_H

#include <stdbool.h>
#include <stdint.h>

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
} VartAddressRegion;

int vart_address_region_init(VartAddressRegion *region,
                             VartAddressRegionType type, uint64_t base,
                             uint64_t size, int priority, void *owner);
void vart_address_region_set_enabled(VartAddressRegion *region, bool enabled);
bool vart_address_region_contains(const VartAddressRegion *region,
                                  uint64_t address, uint64_t size);

#endif
