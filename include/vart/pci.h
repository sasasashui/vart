#ifndef VART_PCI_H
#define VART_PCI_H

#include <stdint.h>

#include "vart/bus.h"

#define VART_PCI_ROOT_BUS_NUMBER UINT8_C(0)

typedef struct VartPciBus {
    VartBus bus;
    uint8_t number;
} VartPciBus;

typedef struct VartPciRootComplex {
    VartDevice parent;
    VartPciBus root_bus;
} VartPciRootComplex;

int vart_pci_root_complex_init(VartPciRootComplex *complex,
                               VartBus *parent_bus,
                               VartAddressSpace *address_space);
int vart_pci_root_complex_destroy(VartPciRootComplex *complex);

#endif
