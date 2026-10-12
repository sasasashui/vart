#ifndef VART_VIRTIO_PCI_H
#define VART_VIRTIO_PCI_H

#include <stdint.h>

#include "vart/eventfd.h"
#include "vart/pci.h"
#include "vart/virtio.h"

struct VartVm;

#define VART_VIRTIO_PCI_VENDOR_ID UINT16_C(0x1af4)
#define VART_VIRTIO_PCI_BAR_SIZE UINT64_C(0x1000)
#define VART_VIRTIO_PCI_COMMON_OFFSET UINT64_C(0x000)
#define VART_VIRTIO_PCI_COMMON_SIZE UINT64_C(0x100)
#define VART_VIRTIO_PCI_NOTIFY_OFFSET UINT64_C(0x100)
#define VART_VIRTIO_PCI_ISR_OFFSET UINT64_C(0x200)
#define VART_VIRTIO_PCI_DEVICE_OFFSET UINT64_C(0x300)
#define VART_VIRTIO_PCI_DEVICE_SIZE UINT64_C(0x100)
#define VART_VIRTIO_PCI_NOTIFY_MULTIPLIER UINT32_C(4)
#define VART_VIRTIO_PCI_NO_VECTOR UINT16_C(0xffff)

typedef struct VartVirtioPciDevice VartVirtioPciDevice;

typedef int (*VartVirtioPciNotify)(VartVirtioPciDevice *device,
                                   unsigned int queue_index, void *opaque);
typedef int (*VartVirtioPciConfigRead)(VartVirtioPciDevice *device,
                                       uint64_t offset, unsigned int size,
                                       uint64_t *value, void *opaque);
typedef int (*VartVirtioPciConfigWrite)(VartVirtioPciDevice *device,
                                        uint64_t offset, unsigned int size,
                                        uint64_t value, void *opaque);

typedef struct VartVirtioPciConfigOps {
    VartVirtioPciConfigRead read;
    VartVirtioPciConfigWrite write;
    void *opaque;
} VartVirtioPciConfigOps;

typedef struct VartVirtioPciQueueState {
    uint16_t size;
    uint16_t msix_vector;
    uint64_t descriptor_address;
    uint64_t available_address;
    uint64_t used_address;
} VartVirtioPciQueueState;

struct VartVirtioPciDevice {
    VartPciDevice pci;
    VartVirtioDevice virtio;
    VartVirtioPciQueueState *queue_state;
    VartVirtioPciNotify notify;
    void *notify_opaque;
    VartVirtioPciConfigOps config_ops;
    uint32_t device_feature_select;
    uint32_t driver_feature_select;
    uint16_t queue_select;
    uint16_t config_msix_vector;
    uint8_t config_generation;
    uint8_t isr_status;
    VartEventNotifier *ioeventfd_notifiers;
    VartEventNotifierSource *ioeventfd_sources;
    bool *ioeventfd_assigned;
    uint64_t *ioeventfd_addresses;
    struct VartVm *ioeventfd_vm;
    bool ioeventfd_enabled;
    bool initialized;
};

int vart_virtio_pci_device_init(VartVirtioPciDevice *device,
                                uint32_t device_id, uint64_t device_features,
                                unsigned int queue_count,
                                const VartVirtioDmaOps *dma,
                                const VartVirtioPciConfigOps *config_ops,
                                VartVirtioPciNotify notify,
                                void *notify_opaque);
int vart_virtio_pci_device_destroy(VartVirtioPciDevice *device);
int vart_virtio_pci_device_set_class_code(VartVirtioPciDevice *device,
                                          uint8_t class_code);
int vart_virtio_pci_raise_queue_interrupt(VartVirtioPciDevice *device,
                                           unsigned int queue_index);
int vart_virtio_pci_raise_interrupt(VartVirtioPciDevice *device,
                                    bool configuration_change);
int vart_virtio_pci_enable_ioeventfd(VartVirtioPciDevice *device,
                                     struct VartVm *vm,
                                     VartEventLoop *loop);
int vart_virtio_pci_disable_ioeventfd(VartVirtioPciDevice *device);

#endif
