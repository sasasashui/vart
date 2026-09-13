#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "vart/address-space.h"

int vart_address_region_init(VartAddressRegion *region,
                             VartAddressRegionType type, uint64_t base,
                             uint64_t size, int priority, void *owner)
{
    if (region == NULL || size == 0 || base > UINT64_MAX - (size - 1) ||
        type > VART_REGION_ALIAS) {
        return -EINVAL;
    }

    memset(region, 0, sizeof(*region));
    region->base = base;
    region->size = size;
    region->priority = priority;
    region->type = type;
    region->enabled = true;
    region->owner = owner;
    return 0;
}

void vart_address_region_set_enabled(VartAddressRegion *region, bool enabled)
{
    region->enabled = enabled;
}

bool vart_address_region_contains(const VartAddressRegion *region,
                                  uint64_t address, uint64_t size)
{
    uint64_t offset;

    if (region == NULL || !region->enabled || address < region->base) {
        return false;
    }

    offset = address - region->base;
    if (offset > region->size) {
        return false;
    }

    /* A zero-length range may name the byte immediately after the region. */
    return size <= region->size - offset;
}
