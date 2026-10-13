#ifndef VART_IOMMU_H
#define VART_IOMMU_H

#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>
#include <stdint.h>

#include "vart/address-space.h"

typedef struct VartPciDevice VartPciDevice;
typedef struct VartIommuContext VartIommuContext;

typedef void (*VartIommuFaultHandler)(VartIommuContext *context,
                                      uint64_t address, size_t size,
                                      bool write, int error, void *opaque);

typedef enum VartIommuContextMode {
    VART_IOMMU_CONTEXT_IDENTITY,
    VART_IOMMU_CONTEXT_FAULT,
    VART_IOMMU_CONTEXT_SV39X4,
} VartIommuContextMode;

#define VART_IOMMU_CACHE_ENTRIES 64

typedef struct VartIommuCacheEntry {
    uint64_t iova_base;
    uint64_t translated_base;
    uint64_t size;
    uint64_t permissions;
    bool valid;
} VartIommuCacheEntry;

struct VartIommuContext {
    VartAddressSpace address_space;
    VartAddressSpace *downstream;
    VartPciDevice *attached_device;
    VartAddressSpace *saved_dma_as;
    bool saved_dma_as_from_bus;
    uint16_t requester_id;
    VartIommuContextMode mode;
    uint64_t root_ppn;
    uint64_t invalidation_generation;
    bool root_valid;
    bool update_ad;
    VartIommuFaultHandler fault_handler;
    void *fault_opaque;
    VartIommuCacheEntry cache[VART_IOMMU_CACHE_ENTRIES];
    size_t cache_count;
    size_t cache_next;
    pthread_mutex_t cache_lock;
    bool cache_lock_initialized;
};

int vart_iommu_context_init(VartIommuContext *context,
                            VartAddressSpace *downstream,
                            VartIommuContextMode mode);
void vart_iommu_context_destroy(VartIommuContext *context);
int vart_iommu_context_set_mode(VartIommuContext *context,
                                 VartIommuContextMode mode);
int vart_iommu_context_set_sv39x4(VartIommuContext *context, uint64_t root_ppn,
                                  bool update_ad);
int vart_iommu_context_invalidate(VartIommuContext *context, uint64_t address,
                                  size_t size);
int vart_iommu_context_set_fault_handler(VartIommuContext *context,
                                         VartIommuFaultHandler handler,
                                         void *opaque);
int vart_iommu_context_translate_address(VartIommuContext *context,
                                         uint64_t address, size_t size,
                                         bool write, uint64_t *translated);
VartAddressSpace *vart_iommu_context_address_space(VartIommuContext *context);
int vart_iommu_context_attach(VartIommuContext *context,
                              VartPciDevice *device);
int vart_iommu_context_detach(VartIommuContext *context,
                              VartPciDevice *device);
bool vart_iommu_context_matches_requester(const VartIommuContext *context,
                                          uint16_t requester_id);

#endif
