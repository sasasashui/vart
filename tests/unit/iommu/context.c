#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vart/address-space.h"
#include "vart/bus.h"
#include "vart/iommu.h"
#include "vart/machine/virt.h"
#include "vart/pci.h"

static int check(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "not ok - %s\n", message);
        return -1;
    }
    return 0;
}

static void store_le64(uint8_t *data, uint64_t value)
{
    unsigned int i;

    for (i = 0; i < sizeof(value); i++) {
        data[i] = value >> (i * 8);
    }
}

static uint64_t load_le64(const uint8_t *data)
{
    uint64_t value = 0;
    unsigned int i;

    for (i = 0; i < sizeof(value); i++) {
        value |= (uint64_t)data[i] << (i * 8);
    }
    return value;
}

struct cache_stress {
    VartIommuContext *context;
    VartPciDevice *device;
    uint64_t iova;
    uint8_t *buffer;
    size_t size;
    atomic_int failures;
};

static void *cache_lookup_thread(void *opaque)
{
    struct cache_stress *stress = opaque;
    unsigned int i;

    for (i = 0; i < 2000; i++) {
        if (vart_pci_device_dma_read(stress->device, stress->iova,
                                     stress->buffer, stress->size) < 0) {
            atomic_fetch_add(&stress->failures, 1);
        }
    }
    return NULL;
}

static void *cache_invalidate_thread(void *opaque)
{
    struct cache_stress *stress = opaque;
    unsigned int i;

    for (i = 0; i < 2000; i++) {
        if (vart_iommu_context_invalidate(stress->context, stress->iova,
                                          stress->size) < 0) {
            atomic_fetch_add(&stress->failures, 1);
        }
    }
    return NULL;
}

int main(void)
{
    uint8_t backing[0x20000] = { 0 };
    uint8_t source[] = "IOMMU context";
    uint8_t result[sizeof(source)] = { 0 };
    VartAddressSpace downstream;
    VartAddressRegion ram;
    VartBus parent_bus;
    VartPciRootComplex complex;
    VartPciDevice device;
    VartIommuContext context;
    uint64_t config_base = VART_VIRT_PCIE_ECAM_BASE + UINT64_C(4);
    uint64_t root_address = VART_VIRT_DRAM_BASE + UINT64_C(0x4000);
    uint64_t level1_address = VART_VIRT_DRAM_BASE + UINT64_C(0x8000);
    uint64_t level0_address = VART_VIRT_DRAM_BASE + UINT64_C(0xc000);
    uint64_t target_address = VART_VIRT_DRAM_BASE + UINT64_C(0x10000);
    uint64_t target2_address = VART_VIRT_DRAM_BASE + UINT64_C(0x11000);
    uint64_t iova = UINT64_C(0x00403000);
    uint64_t root_index = (iova >> 30) & UINT64_C(0x7ff);
    uint64_t level1_index = (iova >> 21) & UINT64_C(0x1ff);
    uint64_t level0_index = (iova >> 12) & UINT64_C(0x1ff);
    uint64_t *root_pte;
    uint64_t *level1_pte;
    uint64_t *level0_pte;
    uint64_t leaf_pte;
    struct cache_stress stress;
    pthread_t lookup_thread;
    pthread_t invalidate_thread;
    int ret;

    vart_address_space_init(&downstream);
    vart_bus_init(&parent_bus, "iommu-parent", NULL, &downstream);
    ret = vart_address_region_init_ram(&ram, VART_VIRT_DRAM_BASE,
                                       sizeof(backing), -1, NULL, backing);
    if (ret < 0 || vart_address_space_add(&downstream, &ram) < 0 ||
        vart_pci_root_complex_init(&complex, &parent_bus, &downstream) < 0 ||
        vart_bus_add_device(&parent_bus, &complex.parent) < 0 ||
        vart_device_realize(&complex.parent) < 0 ||
        vart_pci_device_init(&device, "iommu-context-test", 0x1234, 0x5678,
                             0, 0, 0, 0xff) < 0 ||
        vart_pci_bus_add_device(&complex.root_bus, &device, 0) < 0) {
        return 1;
    }

    root_pte = (uint64_t *)(backing + (root_address - VART_VIRT_DRAM_BASE) +
                            root_index * sizeof(uint64_t));
    level1_pte = (uint64_t *)(backing + (level1_address - VART_VIRT_DRAM_BASE) +
                              level1_index * sizeof(uint64_t));
    level0_pte = (uint64_t *)(backing + (level0_address - VART_VIRT_DRAM_BASE) +
                              level0_index * sizeof(uint64_t));
    store_le64((uint8_t *)root_pte, (level1_address >> 12 << 10) | 1);
    store_le64((uint8_t *)level1_pte, (level0_address >> 12 << 10) | 1);
    leaf_pte = (target_address >> 12 << 10) | UINT64_C(0xd7);
    store_le64((uint8_t *)level0_pte, leaf_pte);

    ret = vart_iommu_context_init(&context, &downstream,
                                  VART_IOMMU_CONTEXT_IDENTITY);
    if (check(ret == 0, "initialize identity context") < 0 ||
        check(vart_iommu_context_attach(&context, &device) == 0,
              "attach context to PCI device") < 0 ||
        check(vart_iommu_context_matches_requester(
                  &context, vart_pci_device_requester_id(&device)),
              "match attached requester ID") < 0 ||
        check(vart_device_realize(&device.parent) == 0,
              "realize device with attached context") < 0) {
        return 1;
    }
    ret = vart_address_space_write(&downstream, config_base, 2, 0x4);
    if (check(ret == 0, "enable bus mastering") < 0 ||
        check(vart_pci_device_dma_write(&device, VART_VIRT_DRAM_BASE, source,
                                        sizeof(source)) == 0,
              "identity context DMA write") < 0 ||
        check(vart_pci_device_dma_read(&device, VART_VIRT_DRAM_BASE, result,
                                       sizeof(result)) == 0,
              "identity context DMA read") < 0 ||
        check(memcmp(source, result, sizeof(source)) == 0,
              "identity context data round trip") < 0 ||
        check(vart_iommu_context_set_sv39x4(&context, root_address >> 12,
                                            false) == 0,
              "enable Sv39x4 translation") < 0 ||
        check(vart_pci_device_dma_write(&device, iova, source,
                                        sizeof(source)) == 0,
              "Sv39x4 page translation write") < 0 ||
        check(vart_pci_device_dma_read(&device, iova, result,
                                       sizeof(result)) == 0,
              "Sv39x4 page translation read") < 0 ||
        check(memcmp(source, result, sizeof(source)) == 0 &&
                  memcmp(source, backing + (target_address -
                                            VART_VIRT_DRAM_BASE),
                         sizeof(source)) == 0,
              "Sv39x4 translated data round trip") < 0 ||
        check((store_le64((uint8_t *)level0_pte,
                          (target2_address >> 12 << 10) | UINT64_C(0xd7)),
              vart_pci_device_dma_write(&device, iova, source,
                                        sizeof(source)) == 0 &&
              memcmp(source, backing + (target_address -
                                        VART_VIRT_DRAM_BASE),
                     sizeof(source)) == 0),
              "cached translation remains stable before invalidation") < 0 ||
        check(vart_iommu_context_invalidate(&context, iova,
                                            sizeof(source)) == 0 &&
              vart_pci_device_dma_write(&device, iova, source,
                                        sizeof(source)) == 0 &&
              memcmp(source, backing + (target2_address -
                                        VART_VIRT_DRAM_BASE),
                     sizeof(source)) == 0,
              "selective invalidation reloads a changed translation") < 0 ||
        check((store_le64((uint8_t *)level0_pte, leaf_pte),
              vart_iommu_context_invalidate(&context, iova,
                                            sizeof(source)) == 0),
              "restore the original leaf mapping") < 0 ||
        check((stress.context = &context,
               stress.device = &device,
               stress.iova = iova,
               stress.buffer = result,
               stress.size = sizeof(source),
               atomic_init(&stress.failures, 0),
               pthread_create(&lookup_thread, NULL, cache_lookup_thread,
                              &stress) == 0 &&
               pthread_create(&invalidate_thread, NULL,
                              cache_invalidate_thread, &stress) == 0),
              "start concurrent lookup and invalidation") < 0 ||
        check((pthread_join(lookup_thread, NULL) == 0 &&
               pthread_join(invalidate_thread, NULL) == 0 &&
               atomic_load(&stress.failures) == 0),
              "serialize concurrent lookup and invalidation") < 0 ||
        check(vart_pci_device_dma_read(&device, iova + 4090, result,
                                       sizeof(result)) == -ERANGE,
              "reject a DMA buffer crossing a leaf boundary") < 0 ||
        check((store_le64((uint8_t *)level0_pte, 0),
              vart_iommu_context_invalidate(&context, iova,
                                            sizeof(source)) == 0 &&
              vart_pci_device_dma_read(&device, iova, result,
                                       sizeof(result)) == -EFAULT),
              "reject an invalid page-table entry") < 0 ||
        check((store_le64((uint8_t *)level0_pte, leaf_pte),
              vart_pci_device_dma_read(&device, UINT64_C(1) << 50, result,
                                       sizeof(result)) == -EFAULT),
              "reject a non-canonical Sv39x4 IOVA") < 0 ||
        check(vart_iommu_context_invalidate(&context, iova,
                                            sizeof(source)) == 0,
              "invalidate a translated range") < 0 ||
        check(load_le64((uint8_t *)level0_pte) == leaf_pte,
              "page table remains stable after invalidation") < 0 ||
        check((store_le64((uint8_t *)level0_pte,
                          leaf_pte & ~UINT64_C(0x4)),
              vart_iommu_context_invalidate(&context, iova,
                                            sizeof(source)) == 0),
              "invalidate permission update") < 0 ||
        check(vart_pci_device_dma_write(&device, iova, source,
                                        sizeof(source)) == -EACCES,
              "Sv39x4 rejects a write without W permission") < 0 ||
        check((store_le64((uint8_t *)level0_pte, leaf_pte),
              vart_iommu_context_set_sv39x4(&context, root_address >> 12,
                                            true) == 0),
              "enable Sv39x4 accessed/dirty updates") < 0 ||
        check((store_le64((uint8_t *)level0_pte,
                          leaf_pte & ~UINT64_C(0xc0)),
              vart_iommu_context_invalidate(&context, iova,
                                            sizeof(source)) == 0,
              vart_pci_device_dma_read(&device, iova, result,
                                       sizeof(result)) == 0),
              "set the accessed bit on a read") < 0 ||
        check((load_le64((uint8_t *)level0_pte) & UINT64_C(0x40)) != 0,
              "accessed bit was written") < 0 ||
        check(vart_pci_device_dma_write(&device, iova, source,
                                        sizeof(source)) == 0,
              "set the dirty bit on a write") < 0 ||
        check((load_le64((uint8_t *)level0_pte) & UINT64_C(0x80)) != 0,
              "dirty bit was written") < 0 ||
        check((store_le64((uint8_t *)level1_pte,
                          (VART_VIRT_DRAM_BASE >> 12 << 10) | UINT64_C(0xd7)),
              vart_iommu_context_invalidate(&context, iova,
                                            sizeof(source)) == 0),
              "install and invalidate a superpage") < 0 ||
        check(vart_pci_device_dma_read(&device, UINT64_C(0x00400000), result,
                                       sizeof(result)) == 0,
              "Sv39x4 superpage translation read") < 0 ||
        check(vart_iommu_context_set_mode(&context,
                                          VART_IOMMU_CONTEXT_FAULT) == 0,
              "switch context to fault mode") < 0 ||
        check(vart_pci_device_dma_read(&device, VART_VIRT_DRAM_BASE, result,
                                       sizeof(result)) == -EACCES,
              "fault context rejects DMA") < 0 ||
        check(vart_iommu_context_detach(&context, &device) == -EINVAL,
              "cannot detach a realized device") < 0) {
        return 1;
    }

    vart_device_unrealize(&device.parent);
    if (check(vart_iommu_context_detach(&context, &device) == 0,
              "detach context after unrealize") < 0 ||
        check(device.parent.dma_as == &downstream &&
                  device.parent.dma_as_from_bus,
              "restore inherited DMA address space") < 0) {
        return 1;
    }
    vart_iommu_context_destroy(&context);
    if (vart_pci_bus_remove_device(&complex.root_bus, &device) < 0 ||
        vart_pci_root_complex_destroy(&complex) < 0) {
        return 1;
    }
    vart_address_space_remove(&downstream, &ram);
    vart_bus_destroy(&parent_bus);
    vart_address_space_destroy(&downstream);
    puts("ok - IOMMU DMA context modes and lifetime");
    return 0;
}
