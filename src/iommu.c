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

typedef struct VartIommuTranslation {
    uint64_t iova_base;
    uint64_t translated_base;
    uint64_t size;
    uint64_t permissions;
} VartIommuTranslation;

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
                                  size_t size, bool write,
                                  VartIommuTranslation *translation)
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
        translation->iova_base = iova & ~page_mask;
        translation->translated_base = ppn << IOMMU_PAGE_SHIFT;
        translation->size = page_mask + 1;
        translation->permissions = permissions;
        if (translation->translated_base > UINT64_MAX -
            (iova & page_mask)) {
            return -EFAULT;
        }
        return 0;
    }
    return -EFAULT;
}

static void iommu_cache_flush(VartIommuContext *context)
{
    memset(context->cache, 0, sizeof(context->cache));
    context->cache_count = 0;
    context->cache_next = 0;
}

static bool iommu_cache_lookup(const VartIommuContext *context,
                               uint64_t address, size_t size, bool write,
                               uint64_t *translated)
{
    size_t i;

    /* A/D updates are architectural side effects of every page-table walk. */
    if (context->update_ad) {
        return false;
    }

    for (i = 0; i < context->cache_count; i++) {
        const VartIommuCacheEntry *entry = &context->cache[i];

        if (!entry->valid || address < entry->iova_base ||
            address > UINT64_MAX - (size - 1) ||
            address + size > entry->iova_base + entry->size ||
            !(entry->permissions & IOMMU_PTE_R) ||
            (write && !(entry->permissions & IOMMU_PTE_W))) {
            continue;
        }
        if (entry->translated_base > UINT64_MAX -
            (address - entry->iova_base)) {
            return false;
        }
        *translated = entry->translated_base +
                      (address - entry->iova_base);
        if (*translated > UINT64_MAX - (size - 1)) {
            return false;
        }
        return true;
    }
    return false;
}

static void iommu_cache_insert(VartIommuContext *context,
                               const VartIommuTranslation *translation)
{
    size_t index;

    if (context->cache_count < VART_IOMMU_CACHE_ENTRIES) {
        index = context->cache_count++;
    } else {
        index = context->cache_next++ % VART_IOMMU_CACHE_ENTRIES;
    }
    context->cache[index].iova_base = translation->iova_base;
    context->cache[index].translated_base = translation->translated_base;
    context->cache[index].size = translation->size;
    context->cache[index].permissions = translation->permissions;
    context->cache[index].valid = true;
}

static bool iommu_cache_ranges_overlap(const VartIommuCacheEntry *entry,
                                       uint64_t address, size_t size)
{
    uint64_t end = address + size;
    uint64_t entry_end = entry->iova_base + entry->size;

    return address < entry_end && entry->iova_base < end;
}

static void iommu_cache_invalidate(VartIommuContext *context,
                                   uint64_t address, size_t size)
{
    size_t i = 0;

    while (i < context->cache_count) {
        if (iommu_cache_ranges_overlap(&context->cache[i], address, size)) {
            size_t remaining = context->cache_count - i - 1;

            if (remaining != 0) {
                memmove(&context->cache[i], &context->cache[i + 1],
                        remaining * sizeof(context->cache[0]));
            }
            context->cache_count--;
            continue;
        }
        i++;
    }
    if (context->cache_count == 0) {
        context->cache_next = 0;
    } else {
        context->cache_next %= context->cache_count;
    }
}

static int iommu_context_translate(void *opaque, uint64_t address,
                                   size_t size, bool write,
                                   VartAddressSpace **target,
                                   uint64_t *translated)
{
    VartIommuContext *context = opaque;
    VartIommuTranslation translation;
    uint64_t lookup_address = address;
    int ret;

    if (context == NULL || target == NULL || translated == NULL || size == 0) {
        return -EINVAL;
    }
    pthread_mutex_lock(&context->cache_lock);
    if (context->mode == VART_IOMMU_CONTEXT_FAULT) {
        ret = -EACCES;
        goto out;
    }
    if (context->downstream == NULL) {
        ret = -ENODEV;
        goto out;
    }
    *target = context->downstream;
    if (context->mode == VART_IOMMU_CONTEXT_SV39X4) {
        if (!context->root_valid) {
            ret = -ENODEV;
            goto out;
        }
        ret = iommu_canonical_iova(address, &lookup_address);
        if (ret < 0) {
            goto out;
        }
        if (iommu_cache_lookup(context, lookup_address, size, write,
                               translated)) {
            ret = 0;
            goto out;
        }
        ret = iommu_translate_sv39x4(context, address, size, write,
                                      &translation);
        if (ret < 0) {
            goto out;
        }
        iommu_cache_insert(context, &translation);
        *translated = translation.translated_base +
                      (lookup_address - translation.iova_base);
    } else if (context->mode == VART_IOMMU_CONTEXT_IDENTITY) {
        *translated = address;
        ret = 0;
    } else {
        ret = -ENODEV;
    }
out:
    pthread_mutex_unlock(&context->cache_lock);
    return ret;
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
    ret = pthread_mutex_init(&context->cache_lock, NULL);
    if (ret != 0) {
        vart_address_space_destroy(&context->address_space);
        return -ret;
    }
    context->cache_lock_initialized = true;
    ret = vart_address_space_set_translator(&context->address_space,
                                             iommu_context_translate, context);
    if (ret < 0) {
        pthread_mutex_destroy(&context->cache_lock);
        context->cache_lock_initialized = false;
        vart_address_space_destroy(&context->address_space);
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
    if (context->cache_lock_initialized) {
        pthread_mutex_lock(&context->cache_lock);
        iommu_cache_flush(context);
        pthread_mutex_unlock(&context->cache_lock);
        pthread_mutex_destroy(&context->cache_lock);
    }
    vart_address_space_destroy(&context->address_space);
    memset(context, 0, sizeof(*context));
}

int vart_iommu_context_set_mode(VartIommuContext *context,
                                VartIommuContextMode mode)
{
    if (context == NULL || mode > VART_IOMMU_CONTEXT_SV39X4) {
        return -EINVAL;
    }
    pthread_mutex_lock(&context->cache_lock);
    if (context->downstream == NULL ||
        (mode == VART_IOMMU_CONTEXT_SV39X4 && !context->root_valid)) {
        pthread_mutex_unlock(&context->cache_lock);
        return -EINVAL;
    }
    context->mode = mode;
    iommu_cache_flush(context);
    pthread_mutex_unlock(&context->cache_lock);
    return 0;
}

int vart_iommu_context_set_sv39x4(VartIommuContext *context, uint64_t root_ppn,
                                  bool update_ad)
{
    if (context == NULL || context->downstream == NULL ||
        root_ppn > (UINT64_MAX >> IOMMU_PAGE_SHIFT) || (root_ppn & 3) != 0) {
        return -EINVAL;
    }
    pthread_mutex_lock(&context->cache_lock);
    context->root_ppn = root_ppn;
    context->root_valid = true;
    context->update_ad = update_ad;
    context->mode = VART_IOMMU_CONTEXT_SV39X4;
    iommu_cache_flush(context);
    pthread_mutex_unlock(&context->cache_lock);
    return 0;
}

int vart_iommu_context_invalidate(VartIommuContext *context, uint64_t address,
                                  size_t size)
{
    uint64_t iova = address;

    if (context == NULL || size == 0 ||
        address > UINT64_MAX - (size - 1)) {
        return -EINVAL;
    }
    pthread_mutex_lock(&context->cache_lock);
    if (context->mode == VART_IOMMU_CONTEXT_SV39X4) {
        const uint64_t iova_limit = UINT64_C(1) << IOMMU_SV39X4_VA_BITS;

        if (iommu_canonical_iova(address, &iova) < 0) {
            pthread_mutex_unlock(&context->cache_lock);
            return -EFAULT;
        }
        if (size > iova_limit || iova > iova_limit - size) {
            pthread_mutex_unlock(&context->cache_lock);
            return -ERANGE;
        }
        iommu_cache_invalidate(context, iova, size);
    } else {
        iommu_cache_flush(context);
    }
    context->invalidation_generation++;
    pthread_mutex_unlock(&context->cache_lock);
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
