#ifndef VART_IOMMU_H
#define VART_IOMMU_H

#include <stdbool.h>
#include <stdint.h>

#include "vart/address-space.h"

typedef struct VartPciDevice VartPciDevice;

typedef enum VartIommuContextMode {
    VART_IOMMU_CONTEXT_IDENTITY,
    VART_IOMMU_CONTEXT_FAULT,
} VartIommuContextMode;

typedef struct VartIommuContext {
    VartAddressSpace address_space;
    VartAddressSpace *downstream;
    VartPciDevice *attached_device;
    VartAddressSpace *saved_dma_as;
    bool saved_dma_as_from_bus;
    uint16_t requester_id;
    VartIommuContextMode mode;
} VartIommuContext;

int vart_iommu_context_init(VartIommuContext *context,
                            VartAddressSpace *downstream,
                            VartIommuContextMode mode);
void vart_iommu_context_destroy(VartIommuContext *context);
int vart_iommu_context_set_mode(VartIommuContext *context,
                                 VartIommuContextMode mode);
VartAddressSpace *vart_iommu_context_address_space(VartIommuContext *context);
int vart_iommu_context_attach(VartIommuContext *context,
                              VartPciDevice *device);
int vart_iommu_context_detach(VartIommuContext *context,
                              VartPciDevice *device);
bool vart_iommu_context_matches_requester(const VartIommuContext *context,
                                          uint16_t requester_id);

#endif
