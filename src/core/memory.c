#define _GNU_SOURCE

#include <errno.h>
#include <linux/kvm.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "vart/memory.h"

static bool memory_region_is_valid(uint64_t guest_addr, size_t size)
{
    long page_size = sysconf(_SC_PAGESIZE);

    if (page_size <= 0 || size == 0) {
        return false;
    }
    if ((guest_addr % (uint64_t)page_size) != 0 ||
        (size % (size_t)page_size) != 0) {
        return false;
    }
    return guest_addr <= UINT64_MAX - size;
}

int vart_memory_region_create(VartMemoryRegion *region, uint64_t guest_addr,
                              size_t size, unsigned int slot)
{
    void *mapping;

    memset(region, 0, sizeof(*region));
    if (!memory_region_is_valid(guest_addr, size)) {
        return -EINVAL;
    }

    mapping = mmap(NULL, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (mapping == MAP_FAILED) {
        return -errno;
    }

    region->host_addr = mapping;
    region->guest_addr = guest_addr;
    region->size = size;
    region->slot = slot;
    return 0;
}

int vart_memory_region_register(VartMemoryRegion *region, const VartVm *vm)
{
    struct kvm_userspace_memory_region kvm_region;

    if (region->host_addr == NULL || region->registered) {
        return -EINVAL;
    }

    memset(&kvm_region, 0, sizeof(kvm_region));
    kvm_region.slot = region->slot;
    kvm_region.guest_phys_addr = region->guest_addr;
    kvm_region.memory_size = region->size;
    kvm_region.userspace_addr = (uintptr_t)region->host_addr;

    if (vart_vm_ioctl(vm, KVM_SET_USER_MEMORY_REGION,
                      (unsigned long)(uintptr_t)&kvm_region) < 0) {
        return -errno;
    }
    region->registered = true;
    return 0;
}

int vart_memory_region_unregister(VartMemoryRegion *region, const VartVm *vm)
{
    struct kvm_userspace_memory_region kvm_region;

    if (!region->registered) {
        return 0;
    }

    memset(&kvm_region, 0, sizeof(kvm_region));
    kvm_region.slot = region->slot;
    kvm_region.guest_phys_addr = region->guest_addr;
    kvm_region.userspace_addr = (uintptr_t)region->host_addr;

    if (vart_vm_ioctl(vm, KVM_SET_USER_MEMORY_REGION,
                      (unsigned long)(uintptr_t)&kvm_region) < 0) {
        return -errno;
    }
    region->registered = false;
    return 0;
}

int vart_memory_region_map(VartMemoryRegion *region,
                           VartAddressSpace *address_space)
{
    int ret;

    if (region == NULL || address_space == NULL || region->host_addr == NULL) {
        return -EINVAL;
    }
    if (region->address_space_mapped) {
        return -EALREADY;
    }
    ret = vart_address_region_init_ram(&region->address_region,
                                       region->guest_addr, region->size,
                                       -1, region, region->host_addr);
    if (ret < 0) {
        return ret;
    }
    ret = vart_address_space_add(address_space, &region->address_region);
    if (ret < 0) {
        memset(&region->address_region, 0, sizeof(region->address_region));
        return ret;
    }
    region->address_space_mapped = true;
    return 0;
}

int vart_memory_region_unmap(VartMemoryRegion *region)
{
    int ret;

    if (region == NULL) {
        return -EINVAL;
    }
    if (!region->address_space_mapped) {
        return 0;
    }
    if (region->address_region.address_space != NULL) {
        ret = vart_address_space_remove(region->address_region.address_space,
                                        &region->address_region);
        if (ret < 0) {
            return ret;
        }
    }
    memset(&region->address_region, 0, sizeof(region->address_region));
    region->address_space_mapped = false;
    return 0;
}

static int memory_region_access(VartMemoryRegion *region, uint64_t guest_addr,
                                void *data, size_t size, bool write)
{
    uint64_t offset;

    if (region == NULL || region->host_addr == NULL ||
        guest_addr < region->guest_addr) {
        return -ERANGE;
    }
    offset = guest_addr - region->guest_addr;
    if (offset > region->size || size > region->size - offset) {
        return -ERANGE;
    }
    if (size == 0) {
        return 0;
    }
    if (data == NULL) {
        return -EINVAL;
    }
    if (write) {
        memcpy((char *)region->host_addr + offset, data, size);
    } else {
        memcpy(data, (const char *)region->host_addr + offset, size);
    }
    return 0;
}

int vart_memory_region_read(VartMemoryRegion *region, uint64_t guest_addr,
                            void *data, size_t size)
{
    return memory_region_access(region, guest_addr, data, size, false);
}

int vart_memory_region_write(VartMemoryRegion *region, uint64_t guest_addr,
                             const void *data, size_t size)
{
    return memory_region_access(region, guest_addr, (void *)data, size, true);
}

void vart_memory_region_destroy(VartMemoryRegion *region)
{
    (void)vart_memory_region_unmap(region);
    if (region->host_addr != NULL) {
        munmap(region->host_addr, region->size);
    }
    memset(region, 0, sizeof(*region));
}
