#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "vart/machine/virt-fdt.h"
#include "vart/machine/virt-loader.h"
#include "vart/memory.h"

static int add_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (left > UINT64_MAX - right) {
        return -EOVERFLOW;
    }
    *result = left + right;
    return 0;
}

static int align_up(uint64_t value, uint64_t alignment, uint64_t *result)
{
    uint64_t mask = alignment - 1;

    if (value > UINT64_MAX - mask) {
        return -EOVERFLOW;
    }
    *result = (value + mask) & ~mask;
    return 0;
}

int vart_virt_boot_layout(const VartVirtBootLayoutConfig *config,
                          VartVirtBootLayout *layout)
{
    VartVirtBootLayout result = { 0 };
    uint64_t initrd_offset;
    uint64_t ram_end;
    uint64_t fdt_floor;
    int ret;

    if (config == NULL || layout == NULL || config->ram_size == 0 ||
        config->kernel_size == 0) {
        return -EINVAL;
    }
    ret = add_u64(config->ram_base, config->ram_size, &ram_end);
    if (ret < 0) {
        return ret;
    }
    if (config->ram_size < VART_VIRT_FDT_MAX_SIZE) {
        return -ENOSPC;
    }
    ret = align_up(config->ram_base, VART_VIRT_BOOT_ALIGN,
                   &result.kernel_start);
    if (ret < 0) {
        return ret;
    }
    ret = add_u64(result.kernel_start, config->kernel_size,
                  &result.kernel_end);
    if (ret < 0) {
        return ret;
    }

    fdt_floor = ram_end - VART_VIRT_FDT_MAX_SIZE;
    result.fdt_start = fdt_floor & ~(VART_VIRT_BOOT_ALIGN - 1);
    if (result.kernel_end > result.fdt_start) {
        return -ENOSPC;
    }

    if (config->initrd_size == 0) {
        *layout = result;
        return 0;
    }
    initrd_offset = config->ram_size / 2;
    if (initrd_offset > VART_VIRT_INITRD_MAX_OFFSET) {
        initrd_offset = VART_VIRT_INITRD_MAX_OFFSET;
    }
    ret = add_u64(result.kernel_start, initrd_offset,
                  &result.initrd_start);
    if (ret < 0) {
        return ret;
    }
    ret = add_u64(result.initrd_start, config->initrd_size,
                  &result.initrd_end);
    if (ret < 0) {
        return ret;
    }
    if (result.kernel_end > result.initrd_start ||
        result.initrd_end > result.fdt_start) {
        return -ENOSPC;
    }
    *layout = result;
    return 0;
}

int vart_virt_boot_load(VartMemoryRegion *ram,
                        const VartVirtBootConfig *config,
                        VartRiscvBootInfo *boot)
{
    VartVirtBootLayoutConfig layout_config;
    VartVirtFdtConfig fdt_config;
    VartRiscvBootInfo result;
    VartVirtBootLayout layout;
    void *fdt = NULL;
    size_t fdt_size;
    int ret;

    if (ram == NULL || config == NULL || boot == NULL ||
        ram->host_addr == NULL || config->kernel == NULL ||
        config->kernel_size == 0 ||
        (config->initrd_size != 0 && config->initrd == NULL)) {
        return -EINVAL;
    }
    layout_config = (VartVirtBootLayoutConfig) {
        .ram_base = ram->guest_addr,
        .ram_size = ram->size,
        .kernel_size = config->kernel_size,
        .initrd_size = config->initrd_size,
    };
    ret = vart_virt_boot_layout(&layout_config, &layout);
    if (ret < 0) {
        return ret;
    }
    fdt_config = (VartVirtFdtConfig) {
        .cpus = config->cpus,
        .cpu_count = config->cpu_count,
        .timebase_frequency = config->timebase_frequency,
        .ram_base = ram->guest_addr,
        .ram_size = ram->size,
        .bootargs = config->bootargs,
        .initrd_start = layout.initrd_start,
        .initrd_size = config->initrd_size,
    };
    ret = vart_virt_fdt_build(&fdt_config, &fdt, &fdt_size);
    if (ret < 0) {
        return ret;
    }
    if (fdt_size > VART_VIRT_FDT_MAX_SIZE) {
        ret = -EFBIG;
        goto out;
    }

    /* All ranges are validated before the first guest-visible write. */
    ret = vart_memory_region_write(ram, layout.kernel_start,
                                   config->kernel, config->kernel_size);
    if (ret == 0 && config->initrd_size != 0) {
        ret = vart_memory_region_write(ram, layout.initrd_start,
                                       config->initrd,
                                       config->initrd_size);
    }
    if (ret == 0) {
        ret = vart_memory_region_write(ram, layout.fdt_start, fdt, fdt_size);
    }
    if (ret == 0) {
        result.entry = layout.kernel_start;
        result.fdt_addr = layout.fdt_start;
        *boot = result;
    }
out:
    free(fdt);
    return ret;
}
