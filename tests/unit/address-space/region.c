#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "vart/address-space.h"

int main(void)
{
    VartAddressRegion region;
    int owner;

    if (vart_address_region_init(NULL, VART_REGION_RAM, 0, 1, 0, NULL) !=
            -EINVAL ||
        vart_address_region_init(&region, VART_REGION_RAM, 0, 0, 0, NULL) !=
            -EINVAL ||
        vart_address_region_init(&region, VART_REGION_RAM, UINT64_MAX, 2,
                                 0, NULL) != -EINVAL ||
        vart_address_region_init(&region, (VartAddressRegionType)99,
                                 0, 1, 0, NULL) != -EINVAL) {
        fprintf(stderr, "not ok - reject invalid address regions\n");
        return EXIT_FAILURE;
    }
    if (vart_address_region_init(&region, VART_REGION_ROM, UINT64_MAX, 1,
                                 0, NULL) != 0 ||
        !vart_address_region_contains(&region, UINT64_MAX, 1)) {
        fprintf(stderr, "not ok - maximum address region\n");
        return EXIT_FAILURE;
    }

    if (vart_address_region_init(&region, VART_REGION_MMIO, 0x1000, 0x100,
                                 7, &owner) != 0 ||
        region.type != VART_REGION_MMIO || region.priority != 7 ||
        region.owner != &owner || !region.enabled ||
        !vart_address_region_contains(&region, 0x1000, 1) ||
        !vart_address_region_contains(&region, 0x10ff, 1) ||
        vart_address_region_contains(&region, 0x0fff, 1) ||
        vart_address_region_contains(&region, 0x10ff, 2) ||
        !vart_address_region_contains(&region, 0x1100, 0)) {
        fprintf(stderr, "not ok - address region range semantics\n");
        return EXIT_FAILURE;
    }

    if (vart_address_region_set_enabled(&region, false) != 0) {
        return EXIT_FAILURE;
    }
    if (vart_address_region_contains(&region, 0x1000, 1)) {
        fprintf(stderr, "not ok - disabled address region is visible\n");
        return EXIT_FAILURE;
    }

    printf("ok - validate generic address regions\n");
    return EXIT_SUCCESS;
}
