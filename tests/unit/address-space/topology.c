#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "vart/address-space.h"

int main(void)
{
    VartAddressRegion base;
    VartAddressRegion adjacent;
    VartAddressRegion overlay;
    VartAddressRegion conflict;
    VartAddressSpace address_space;
    VartAddressRegion growth[9];
    VartAddressSpace growth_space;
    size_t i;

    vart_address_space_init(&address_space);
    vart_address_region_init(&base, VART_REGION_RAM, 0x1000, 0x1000, 0, NULL);
    vart_address_region_init(&adjacent, VART_REGION_MMIO, 0x2000, 0x100, 0,
                             NULL);
    vart_address_region_init(&overlay, VART_REGION_ROM, 0x1800, 0x100, 10,
                             NULL);
    vart_address_region_init(&conflict, VART_REGION_MMIO, 0x1f00, 0x200, 0,
                             NULL);

    if (vart_address_space_add(&address_space, &base) != 0 ||
        vart_address_space_add(&address_space, &adjacent) != 0 ||
        vart_address_space_add(&address_space, &overlay) != 0 ||
        vart_address_space_add(&address_space, &conflict) != -EEXIST ||
        vart_address_space_add(&address_space, &base) != -EALREADY ||
        vart_address_region_set_enabled(&base, false) != -EBUSY) {
        fprintf(stderr, "not ok - register address-space topology\n");
        return EXIT_FAILURE;
    }

    if (vart_address_space_find(&address_space, 0x1000, 1) != &base ||
        vart_address_space_find(&address_space, 0x1800, 1) != &overlay ||
        vart_address_space_find(&address_space, 0x1fff, 1) != &base ||
        vart_address_space_find(&address_space, 0x2000, 1) != &adjacent ||
        vart_address_space_find(&address_space, 0x3000, 1) != NULL) {
        fprintf(stderr, "not ok - lookup address-space topology\n");
        return EXIT_FAILURE;
    }

    if (vart_address_space_set_enabled(&address_space, &overlay, false) != 0 ||
        vart_address_space_find(&address_space, 0x1800, 1) != &base ||
        vart_address_space_remove(&address_space, &base) != 0 ||
        vart_address_space_find(&address_space, 0x1000, 1) != NULL ||
        vart_address_space_remove(&address_space, &base) != -ENOENT) {
        fprintf(stderr, "not ok - mutate address-space topology\n");
        return EXIT_FAILURE;
    }

    vart_address_space_destroy(&address_space);
    if (adjacent.address_space != NULL || overlay.address_space != NULL) {
        fprintf(stderr, "not ok - destroy address-space topology\n");
        return EXIT_FAILURE;
    }

    vart_address_space_init(&growth_space);
    for (i = 0; i < sizeof(growth) / sizeof(growth[0]); i++) {
        vart_address_region_init(&growth[i], VART_REGION_RAM,
                                 0x10000 + i * 0x1000, 0x1000, 0, NULL);
        if (vart_address_space_add(&growth_space, &growth[i]) != 0) {
            fprintf(stderr, "not ok - grow address-space topology\n");
            return EXIT_FAILURE;
        }
    }
    if (growth_space.count != 9 || growth_space.capacity < 9 ||
        vart_address_space_find(&growth_space, 0x18000, 1) != &growth[8]) {
        fprintf(stderr, "not ok - lookup grown address-space topology\n");
        return EXIT_FAILURE;
    }
    vart_address_space_destroy(&growth_space);

    printf("ok - register and lookup address-space topology\n");
    return EXIT_SUCCESS;
}
