#include <errno.h>
#include <string.h>

#include "vart/iommu.h"
#include "vart/pci.h"

static int iommu_context_translate(void *opaque, uint64_t address,
                                   size_t size, bool write,
                                   VartAddressSpace **target,
                                   uint64_t *translated)
{
    VartIommuContext *context = opaque;

    (void)write;
    if (context == NULL || target == NULL || translated == NULL || size == 0) {
        return -EINVAL;
    }
    if (context->mode == VART_IOMMU_CONTEXT_FAULT) {
        return -EACCES;
    }
    if (context->mode != VART_IOMMU_CONTEXT_IDENTITY ||
        context->downstream == NULL) {
        return -ENODEV;
    }
    *target = context->downstream;
    *translated = address;
    return 0;
}

int vart_iommu_context_init(VartIommuContext *context,
                            VartAddressSpace *downstream,
                            VartIommuContextMode mode)
{
    int ret;

    if (context == NULL || downstream == NULL ||
        downstream == &context->address_space ||
        mode > VART_IOMMU_CONTEXT_FAULT) {
        return -EINVAL;
    }
    memset(context, 0, sizeof(*context));
    vart_address_space_init(&context->address_space);
    ret = vart_address_space_set_translator(&context->address_space,
                                             iommu_context_translate, context);
    if (ret < 0) {
        return ret;
    }
    context->downstream = downstream;
    context->mode = mode;
    return 0;
}

void vart_iommu_context_destroy(VartIommuContext *context)
{
    if (context == NULL) {
        return;
    }
    if (context->attached_device != NULL) {
        return;
    }
    vart_address_space_destroy(&context->address_space);
    memset(context, 0, sizeof(*context));
}

int vart_iommu_context_set_mode(VartIommuContext *context,
                                VartIommuContextMode mode)
{
    if (context == NULL || context->downstream == NULL ||
        mode > VART_IOMMU_CONTEXT_FAULT) {
        return -EINVAL;
    }
    context->mode = mode;
    return 0;
}

VartAddressSpace *vart_iommu_context_address_space(VartIommuContext *context)
{
    return context != NULL ? &context->address_space : NULL;
}

int vart_iommu_context_attach(VartIommuContext *context,
                              VartPciDevice *device)
{
    int ret;

    if (context == NULL || device == NULL || context->attached_device != NULL ||
        device->parent.realized) {
        return -EINVAL;
    }
    context->saved_dma_as = device->parent.dma_as;
    context->saved_dma_as_from_bus = device->parent.dma_as_from_bus;
    ret = vart_pci_device_set_dma_as(device, &context->address_space);
    if (ret < 0) {
        return ret;
    }
    context->attached_device = device;
    context->requester_id = vart_pci_device_requester_id(device);
    return 0;
}

int vart_iommu_context_detach(VartIommuContext *context,
                              VartPciDevice *device)
{
    if (context == NULL || device == NULL ||
        context->attached_device != device || device->parent.realized) {
        return -EINVAL;
    }
    device->parent.dma_as = context->saved_dma_as;
    device->parent.dma_as_from_bus = context->saved_dma_as_from_bus;
    context->attached_device = NULL;
    context->saved_dma_as = NULL;
    context->saved_dma_as_from_bus = false;
    return 0;
}

bool vart_iommu_context_matches_requester(const VartIommuContext *context,
                                          uint16_t requester_id)
{
    return context != NULL && context->attached_device != NULL &&
           context->requester_id == requester_id &&
           vart_pci_device_requester_id(context->attached_device) ==
               requester_id;
}
