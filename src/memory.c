#define _GNU_SOURCE

#include <errno.h>
#include <linux/kvm.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
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

    if (ioctl(vm->fd, KVM_SET_USER_MEMORY_REGION, &kvm_region) < 0) {
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

    if (ioctl(vm->fd, KVM_SET_USER_MEMORY_REGION, &kvm_region) < 0) {
        return -errno;
    }
    region->registered = false;
    return 0;
}

void vart_memory_region_destroy(VartMemoryRegion *region)
{
    if (region->host_addr != NULL) {
        munmap(region->host_addr, region->size);
    }
    memset(region, 0, sizeof(*region));
}
