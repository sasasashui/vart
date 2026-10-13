#include <errno.h>
#include <string.h>

#include "vart/iommu.h"
#include "vart/pci.h"

#define IOMMU_PAGE_SHIFT 12
#define IOMMU_PTE_SIZE 8
#define IOMMU_PT_INDEX_BITS 9
#define IOMMU_SV39X4_ROOT_BITS 11
#define IOMMU_SV39X4_LEVELS 3
#define IOMMU_SV39X4_VA_BITS 41

#define IOMMU_PTE_V UINT64_C(0x001)
#define IOMMU_PTE_R UINT64_C(0x002)
#define IOMMU_PTE_W UINT64_C(0x004)
#define IOMMU_PTE_X UINT64_C(0x008)
#define IOMMU_PTE_U UINT64_C(0x010)
#define IOMMU_PTE_A UINT64_C(0x040)
#define IOMMU_PTE_D UINT64_C(0x080)
#define IOMMU_PTE_PPN_MASK UINT64_C(0x003ffffffffffc00)
#define IOMMU_PTE_RESERVED UINT64_C(0x1fc0000000000000)
#define IOMMU_PTE_N UINT64_C(0x8000000000000000)

static uint64_t load_le64(const uint8_t *data)
{
    uint64_t value = 0;
    unsigned int i;

    for (i = 0; i < sizeof(value); i++) {
        value |= (uint64_t)data[i] << (i * 8);
    }
    return value;
}

static void store_le64(uint8_t *data, uint64_t value)
{
    unsigned int i;

    for (i = 0; i < sizeof(value); i++) {
        data[i] = value >> (i * 8);
    }
}

static int iommu_read_pte(VartIommuContext *context, uint64_t address,
                          uint64_t *pte)
{
    uint8_t data[IOMMU_PTE_SIZE];
    int ret;

    ret = vart_address_space_read_buffer(context->downstream, address, data,
                                         sizeof(data));
    if (ret < 0) {
        return ret;
    }
    *pte = load_le64(data);
    return 0;
}

static int iommu_write_pte(VartIommuContext *context, uint64_t address,
                           uint64_t pte)
{
    uint8_t data[IOMMU_PTE_SIZE];

    store_le64(data, pte);
    return vart_address_space_write_buffer(context->downstream, address, data,
                                            sizeof(data));
}

static int iommu_canonical_iova(uint64_t address, uint64_t *iova)
{
    const uint64_t low_mask = (UINT64_C(1) << IOMMU_SV39X4_VA_BITS) - 1;
    const uint64_t upper = address >> (IOMMU_SV39X4_VA_BITS - 1);
    const uint64_t sign_extended = UINT64_MAX >> (IOMMU_SV39X4_VA_BITS - 1);

    if (upper != 0 && upper != sign_extended) {
        return -EFAULT;
    }
    *iova = address & low_mask;
    return 0;
}

/* Walk a three-level Sv39x4 table; the widened root has eleven index bits. */
static int iommu_translate_sv39x4(VartIommuContext *context, uint64_t address,
                                  size_t size, bool write, uint64_t *translated)
{
    uint64_t iova;
    uint64_t table;
    unsigned int level;
    int ret;

    ret = iommu_canonical_iova(address, &iova);
    if (ret < 0) {
        return ret;
    }
    table = context->root_ppn << IOMMU_PAGE_SHIFT;
    for (level = 0; level < IOMMU_SV39X4_LEVELS; level++) {
        unsigned int index_bits = level == 0 ? IOMMU_SV39X4_ROOT_BITS :
                                   IOMMU_PT_INDEX_BITS;
        unsigned int shift = IOMMU_PAGE_SHIFT + IOMMU_PT_INDEX_BITS *
                             (IOMMU_SV39X4_LEVELS - 1 - level);
        uint64_t index = (iova >> shift) & ((UINT64_C(1) << index_bits) - 1);
        uint64_t pte_address;
        uint64_t pte;
        uint64_t ppn;
        uint64_t permissions;
        uint64_t page_mask;

        if (table > UINT64_MAX - index * IOMMU_PTE_SIZE) {
            return -EFAULT;
        }
        pte_address = table + index * IOMMU_PTE_SIZE;
        ret = iommu_read_pte(context, pte_address, &pte);
        if (ret < 0) {
            return ret;
        }
        if (!(pte & IOMMU_PTE_V) || (pte & IOMMU_PTE_RESERVED) ||
            (pte & IOMMU_PTE_N)) {
            return -EFAULT;
        }

        permissions = pte & (IOMMU_PTE_R | IOMMU_PTE_W | IOMMU_PTE_X);
        ppn = (pte & IOMMU_PTE_PPN_MASK) >> 10;
        if (permissions == 0) {
            if (level == IOMMU_SV39X4_LEVELS - 1 ||
                ppn > (UINT64_MAX >> IOMMU_PAGE_SHIFT)) {
                return -EFAULT;
            }
            table = ppn << IOMMU_PAGE_SHIFT;
            continue;
        }
        if (permissions == IOMMU_PTE_W ||
            permissions == (IOMMU_PTE_W | IOMMU_PTE_X) ||
            !(pte & IOMMU_PTE_U) ||
            (write ? !(pte & IOMMU_PTE_W) : !(pte & IOMMU_PTE_R))) {
            return -EACCES;
        }
        if (!(pte & IOMMU_PTE_A) || (write && !(pte & IOMMU_PTE_D))) {
            if (!context->update_ad) {
                return -EFAULT;
            }
            pte |= IOMMU_PTE_A;
            if (write) {
                pte |= IOMMU_PTE_D;
            }
            ret = iommu_write_pte(context, pte_address, pte);
            if (ret < 0) {
                return ret;
            }
        }
        if (ppn > (UINT64_MAX >> IOMMU_PAGE_SHIFT)) {
            return -EFAULT;
        }
        if ((ppn & ((UINT64_C(1) << (shift - IOMMU_PAGE_SHIFT)) - 1)) != 0) {
            return -EFAULT;
        }
        page_mask = (UINT64_C(1) << shift) - 1;
        if ((iova & page_mask) > UINT64_MAX - (size - 1) ||
            (iova & page_mask) + size > page_mask + 1) {
            return -ERANGE;
        }
        *translated = (ppn << IOMMU_PAGE_SHIFT) | (iova & page_mask);
        if (*translated > UINT64_MAX - (size - 1)) {
            return -EFAULT;
        }
        return 0;
    }
    return -EFAULT;
}

static int iommu_context_translate(void *opaque, uint64_t address,
                                   size_t size, bool write,
                                   VartAddressSpace **target,
                                   uint64_t *translated)
{
    VartIommuContext *context = opaque;
    int ret;

    (void)write;
    if (context == NULL || target == NULL || translated == NULL || size == 0) {
        return -EINVAL;
    }
    if (context->mode == VART_IOMMU_CONTEXT_FAULT) {
        return -EACCES;
    }
    if (context->downstream == NULL) {
        return -ENODEV;
    }
    if (context->mode == VART_IOMMU_CONTEXT_SV39X4) {
        if (!context->root_valid) {
            return -ENODEV;
        }
        ret = iommu_translate_sv39x4(context, address, size, write,
                                     translated);
        if (ret < 0) {
            return ret;
        }
    } else if (context->mode == VART_IOMMU_CONTEXT_IDENTITY) {
        *translated = address;
    } else {
        return -ENODEV;
    }
    *target = context->downstream;
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
        mode > VART_IOMMU_CONTEXT_SV39X4 ||
        (mode == VART_IOMMU_CONTEXT_SV39X4 && !context->root_valid)) {
        return -EINVAL;
    }
    context->mode = mode;
    return 0;
}

int vart_iommu_context_set_sv39x4(VartIommuContext *context, uint64_t root_ppn,
                                  bool update_ad)
{
    if (context == NULL || context->downstream == NULL ||
        root_ppn > (UINT64_MAX >> IOMMU_PAGE_SHIFT) || (root_ppn & 3) != 0) {
        return -EINVAL;
    }
    context->root_ppn = root_ppn;
    context->root_valid = true;
    context->update_ad = update_ad;
    context->mode = VART_IOMMU_CONTEXT_SV39X4;
    return 0;
}

int vart_iommu_context_invalidate(VartIommuContext *context, uint64_t address,
                                  size_t size)
{
    uint64_t ignored;

    if (context == NULL || size == 0 ||
        address > UINT64_MAX - (size - 1)) {
        return -EINVAL;
    }
    if (context->mode == VART_IOMMU_CONTEXT_SV39X4 &&
        iommu_canonical_iova(address, &ignored) < 0) {
        return -EFAULT;
    }
    context->invalidation_generation++;
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
