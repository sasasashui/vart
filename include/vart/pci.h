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
#define VART_PCI_CAPABILITY_LIST_OFFSET UINT8_C(0x34)
#define VART_PCI_CAPABILITY_START UINT16_C(0x40)
#define VART_PCI_CAPABILITY_END UINT16_C(0x100)
#define VART_PCI_STATUS_CAPABILITIES UINT16_C(0x0010)
#define VART_PCI_CAP_ID_MSIX UINT8_C(0x11)
#define VART_PCI_MSIX_MAX_VECTORS 2048

typedef struct VartPciDevice VartPciDevice;
typedef struct VartPciBar VartPciBar;
typedef struct VartPciMsix VartPciMsix;

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

struct VartPciMsix {
    VartAddressRegion table_region;
    VartAddressRegion pba_region;
    uint8_t *table;
    uint8_t *pba;
    uint32_t table_offset;
    uint32_t pba_offset;
    uint8_t table_bar;
    uint8_t pba_bar;
    uint8_t capability_offset;
    unsigned int vector_count;
    bool configured;
    bool mapped;
    bool enabled;
    bool function_mask;
};

struct VartPciDevice {
    VartDevice parent;
    VartPciBus *pci_bus;
    uint8_t devfn;
    uint8_t config_space[VART_PCI_CONFIG_SPACE_SIZE];
    VartPciBar bars[VART_PCI_BAR_COUNT];
    const VartPciConfigOps *config_ops;
    uint16_t capability_next;
    uint8_t capability_tail;
    VartPciMsix msix;
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
int vart_pci_device_add_capability(VartPciDevice *device, uint8_t id,
                                   const void *data, unsigned int data_size,
                                   uint8_t *offset);
int vart_pci_device_init_msix(VartPciDevice *device, unsigned int vector_count,
                               unsigned int table_bar, uint32_t table_offset,
                               unsigned int pba_bar, uint32_t pba_offset);

#endif
