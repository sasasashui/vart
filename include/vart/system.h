#ifndef VART_SYSTEM_H
#define VART_SYSTEM_H

#include "vart/address-space.h"

/*
 * Process-wide machine objects.  VART intentionally supports one active VM
 * per process, so system-level objects have a stable owner and lifetime.
 */
typedef struct VartSystem {
    VartAddressSpace system_address_space;
} VartSystem;

extern VartSystem vart_system;

#endif
