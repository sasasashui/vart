#ifndef VART_PCI_MSIX_IRQFD_H
#define VART_PCI_MSIX_IRQFD_H

#include <stdbool.h>
#include <stdint.h>

#include "vart/eventfd.h"
#include "vart/kvm-irq-routing.h"
#include "vart/pci.h"

typedef struct VartPciMsixIrqfd {
    VartPciDevice *device;
    VartKvmIrqRouting *routing;
    VartEventNotifier *notifiers;
    bool *route_added;
    bool *assigned;
    uint32_t first_gsi;
    unsigned int vector_count;
    bool connected;
} VartPciMsixIrqfd;

int vart_pci_msix_irqfd_init(VartPciMsixIrqfd *backend,
                             VartPciDevice *device,
                             VartKvmIrqRouting *routing,
                             uint32_t first_gsi);
int vart_pci_msix_irqfd_destroy(VartPciMsixIrqfd *backend);

#endif
