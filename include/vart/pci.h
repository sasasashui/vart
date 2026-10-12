#ifndef VART_PCI_H
#define VART_PCI_H

#include <stdint.h>

#include "vart/bus.h"

#define VART_PCI_ROOT_BUS_NUMBER UINT8_C(0)
#define VART_PCI_CONFIG_SPACE_SIZE UINT64_C(0x1000)
#define VART_PCI_MAX_FUNCTIONS 256
#define VART_PCI_BAR_COUNT 6
#define VART_PCI_BAR_MEM_64 UINT32_C(0x4)
#define VART_PCI_BAR_PREFETCHABLE UINT32_C(0x8)

typedef struct VartPciDevice VartPciDevice;
typedef struct VartPciBar VartPciBar;

/* Return -ENOENT when the generic configuration space should handle access. */
typedef int (*VartPciConfigRead)(VartPciDevice *device, unsigned int offset,
                                unsigned int size, uint64_t *value);
typedef int (*VartPciConfigWrite)(VartPciDevice *device, unsigned int offset,
                                 unsigned int size, uint64_t value);

typedef struct VartPciConfigOps {
    VartPciConfigRead read;
    VartPciConfigWrite write;
} VartPciConfigOps;

typedef struct VartPciBus {
    VartBus bus;
    uint8_t number;
    VartPciDevice *functions[VART_PCI_MAX_FUNCTIONS];
    uint64_t mmio_base;
    uint64_t mmio_size;
    uint64_t mmio_next;
} VartPciBus;

struct VartPciBar {
    VartAddressRegion region;
    uint64_t size;
    uint64_t address;
    uint32_t flags;
    const VartMmioOps *mmio_ops;
    void *mmio_opaque;
    bool implemented;
    bool high_part;
    bool is_64bit;
    bool mapped;
    bool probe;
};

struct VartPciDevice {
    VartDevice parent;
    VartPciBus *pci_bus;
    uint8_t devfn;
    uint8_t config_space[VART_PCI_CONFIG_SPACE_SIZE];
    VartPciBar bars[VART_PCI_BAR_COUNT];
    const VartPciConfigOps *config_ops;
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
int vart_pci_device_set_bar(VartPciDevice *device, unsigned int index,
                            uint64_t size, uint32_t flags,
                            const VartMmioOps *mmio_ops, void *mmio_opaque);
int vart_pci_device_set_config_ops(VartPciDevice *device,
                                   const VartPciConfigOps *config_ops);

#endif
