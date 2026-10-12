#ifndef VART_PCI_H
#define VART_PCI_H

#include <stdint.h>

#include "vart/bus.h"

#define VART_PCI_ROOT_BUS_NUMBER UINT8_C(0)
#define VART_PCI_CONFIG_SPACE_SIZE UINT64_C(0x1000)
#define VART_PCI_MAX_FUNCTIONS 256

typedef struct VartPciDevice VartPciDevice;

typedef struct VartPciBus {
    VartBus bus;
    uint8_t number;
    VartPciDevice *functions[VART_PCI_MAX_FUNCTIONS];
} VartPciBus;

struct VartPciDevice {
    VartDevice parent;
    VartPciBus *pci_bus;
    uint8_t devfn;
    uint8_t config_space[VART_PCI_CONFIG_SPACE_SIZE];
};

typedef struct VartPciRootComplex {
    VartDevice parent;
    VartPciBus root_bus;
    VartAddressRegion ecam_region;
    bool ecam_mapped;
} VartPciRootComplex;

int vart_pci_root_complex_init(VartPciRootComplex *complex,
                               VartBus *parent_bus,
                               VartAddressSpace *address_space);
int vart_pci_root_complex_destroy(VartPciRootComplex *complex);
int vart_pci_device_init(VartPciDevice *device, const char *name,
                         uint16_t vendor_id, uint16_t device_id,
                         uint8_t revision, uint8_t prog_if,
                         uint8_t subclass, uint8_t class_code);
int vart_pci_bus_add_device(VartPciBus *bus, VartPciDevice *device,
                            uint8_t devfn);
int vart_pci_bus_remove_device(VartPciBus *bus, VartPciDevice *device);

#endif
