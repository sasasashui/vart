#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
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

int vart_address_region_init_mmio(VartAddressRegion *region, uint64_t base,
                                  uint64_t size, int priority, void *owner,
                                  const VartMmioOps *ops, void *opaque)
{
    int ret;

    if (ops == NULL || (ops->read == NULL && ops->write == NULL)) {
        return -EINVAL;
    }
    ret = vart_address_region_init(region, VART_REGION_MMIO, base, size,
                                   priority, owner);
    if (ret < 0) {
        return ret;
    }
    region->mmio_ops = ops;
    region->mmio_opaque = opaque;
    return 0;
}

int vart_address_region_set_enabled(VartAddressRegion *region, bool enabled)
{
    if (region == NULL || region->address_space != NULL) {
        return -EBUSY;
    }
    region->enabled = enabled;
    return 0;
}

void vart_address_space_init(VartAddressSpace *address_space)
{
    memset(address_space, 0, sizeof(*address_space));
}

void vart_address_space_destroy(VartAddressSpace *address_space)
{
    size_t i;

    for (i = 0; i < address_space->count; i++) {
        address_space->regions[i]->address_space = NULL;
    }
    free(address_space->regions);
    memset(address_space, 0, sizeof(*address_space));
}

static bool address_regions_overlap(const VartAddressRegion *a,
                                    const VartAddressRegion *b)
{
    uint64_t a_last = a->base + a->size - 1;
    uint64_t b_last = b->base + b->size - 1;

    return a->base <= b_last && b->base <= a_last;
}

int vart_address_space_add(VartAddressSpace *address_space,
                           VartAddressRegion *region)
{
    VartAddressRegion **regions;
    size_t capacity;
    size_t i;

    if (address_space == NULL || region == NULL) {
        return -EINVAL;
    }
    if (region->address_space != NULL) {
        return -EALREADY;
    }

    /* Equal-priority overlap is ambiguous; explicit overlays need priority. */
    for (i = 0; i < address_space->count; i++) {
        if (region->priority == address_space->regions[i]->priority &&
            address_regions_overlap(region, address_space->regions[i])) {
            return -EEXIST;
        }
    }

    if (address_space->count == address_space->capacity) {
        capacity = address_space->capacity == 0 ? 8 :
                   address_space->capacity * 2;
        if (capacity < address_space->capacity ||
            capacity > SIZE_MAX / sizeof(*regions)) {
            return -EOVERFLOW;
        }
        regions = realloc(address_space->regions,
                          capacity * sizeof(*regions));
        if (regions == NULL) {
            return -ENOMEM;
        }
        address_space->regions = regions;
        address_space->capacity = capacity;
    }

    address_space->regions[address_space->count++] = region;
    region->address_space = address_space;
    return 0;
}

int vart_address_space_remove(VartAddressSpace *address_space,
                              VartAddressRegion *region)
{
    size_t i;

    if (address_space == NULL || region == NULL ||
        region->address_space != address_space) {
        return -ENOENT;
    }
    for (i = 0; i < address_space->count; i++) {
        if (address_space->regions[i] == region) {
            memmove(&address_space->regions[i],
                    &address_space->regions[i + 1],
                    (address_space->count - i - 1) *
                    sizeof(*address_space->regions));
            address_space->count--;
            region->address_space = NULL;
            return 0;
        }
    }
    return -ENOENT;
}

int vart_address_space_set_enabled(VartAddressSpace *address_space,
                                   VartAddressRegion *region, bool enabled)
{
    if (address_space == NULL || region == NULL ||
        region->address_space != address_space) {
        return -ENOENT;
    }

    /* This is the future topology-listener notification boundary. */
    region->enabled = enabled;
    return 0;
}

VartAddressRegion *vart_address_space_find(const VartAddressSpace *address_space,
                                           uint64_t address, uint64_t size)
{
    VartAddressRegion *best = NULL;
    size_t i;

    if (address_space == NULL) {
        return NULL;
    }
    for (i = 0; i < address_space->count; i++) {
        VartAddressRegion *region = address_space->regions[i];

        if (vart_address_region_contains(region, address, size) &&
            (best == NULL || region->priority > best->priority)) {
            best = region;
        }
    }
    return best;
}

static bool mmio_access_size_valid(unsigned int size)
{
    return size == 1 || size == 2 || size == 4 || size == 8;
}

static uint64_t mmio_value_mask(unsigned int size)
{
    return size == 8 ? UINT64_MAX : (UINT64_C(1) << (size * 8)) - 1;
}

int vart_address_space_read(const VartAddressSpace *address_space,
                            uint64_t address, unsigned int size,
                            uint64_t *value)
{
    VartAddressRegion *region;
    int ret;

    if (!mmio_access_size_valid(size) || value == NULL) {
        return -EINVAL;
    }
    region = vart_address_space_find(address_space, address, size);
    if (region == NULL) {
        return -ENOENT;
    }
    if (region->type != VART_REGION_MMIO) {
        return -EACCES;
    }
    if (region->mmio_ops == NULL || region->mmio_ops->read == NULL) {
        return -ENOSYS;
    }

    ret = region->mmio_ops->read(region->mmio_opaque,
                                 address - region->base, size, value);
    if (ret < 0) {
        return ret;
    }
    *value &= mmio_value_mask(size);
    return 0;
}

int vart_address_space_write(const VartAddressSpace *address_space,
                             uint64_t address, unsigned int size,
                             uint64_t value)
{
    VartAddressRegion *region;

    if (!mmio_access_size_valid(size)) {
        return -EINVAL;
    }
    region = vart_address_space_find(address_space, address, size);
    if (region == NULL) {
        return -ENOENT;
    }
    if (region->type != VART_REGION_MMIO) {
        return -EACCES;
    }
    if (region->mmio_ops == NULL || region->mmio_ops->write == NULL) {
        return -ENOSYS;
    }

    return region->mmio_ops->write(region->mmio_opaque,
                                   address - region->base, size,
                                   value & mmio_value_mask(size));
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
