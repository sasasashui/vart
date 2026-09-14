#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vart/machine/virt-loader.h"
#include "vart/machine/virt.h"

#define FDT_MAGIC UINT32_C(0xd00dfeed)

static uint32_t read_be32(const void *data)
{
    const uint8_t *bytes = data;

    return (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 |
           (uint32_t)bytes[2] << 8 | bytes[3];
}

static int check_load(void)
{
    static const char *const extensions[] = {
        "i", "m", "a", "f", "d", "c", "sstc",
    };
    static const VartVirtFdtCpu cpu = {
        .hartid = 0,
        .isa = "rv64imafdc_sstc",
        .isa_base = "rv64i",
        .isa_extensions = extensions,
        .isa_extension_count = sizeof(extensions) / sizeof(extensions[0]),
        .mmu_type = "riscv,sv48",
    };
    static const uint8_t kernel[] = { 0x13, 0, 0, 0 };
    static const uint8_t initrd[] = { 1, 2, 3, 4, 5 };
    VartVirtBootConfig config = {
        .kernel = kernel,
        .kernel_size = sizeof(kernel),
        .initrd = initrd,
        .initrd_size = sizeof(initrd),
        .cpus = &cpu,
        .cpu_count = 1,
        .timebase_frequency = 10000000,
        .bootargs = "console=ttyS0",
    };
    VartRiscvBootInfo boot = { .entry = UINT64_MAX };
    VartMemoryRegion ram;
    uint8_t *host;
    size_t fdt_offset;
    int ret;

    ret = vart_memory_region_create(&ram, VART_VIRT_DRAM_BASE,
                                    UINT64_C(0x04000000), 0);
    if (ret < 0) {
        return ret;
    }
    host = ram.host_addr;
    ret = vart_virt_boot_load(&ram, &config, &boot);
    fdt_offset = (size_t)(boot.fdt_addr - ram.guest_addr);
    if (ret < 0 || boot.entry != VART_VIRT_DRAM_BASE ||
        boot.fdt_addr != UINT64_C(0x83e00000) ||
        memcmp(host, kernel, sizeof(kernel)) != 0 ||
        memcmp(host + UINT64_C(0x02000000), initrd,
               sizeof(initrd)) != 0 ||
        read_be32(host + fdt_offset) != FDT_MAGIC ||
        read_be32(host + fdt_offset + 4) > VART_VIRT_FDT_MAX_SIZE) {
        ret = -EIO;
        goto out;
    }

    memset(host, 0xa5, sizeof(kernel));
    boot.entry = UINT64_MAX;
    config.cpu_count = 0;
    ret = vart_virt_boot_load(&ram, &config, &boot);
    if (ret != -EINVAL || boot.entry != UINT64_MAX ||
        host[0] != 0xa5) {
        ret = -EIO;
        goto out;
    }
    config.cpu_count = 1;
    config.kernel = NULL;
    if (vart_virt_boot_load(&ram, &config, &boot) != -EINVAL) {
        ret = -EIO;
        goto out;
    }
    config.kernel = kernel;
    config.initrd = NULL;
    if (vart_virt_boot_load(&ram, &config, &boot) != -EINVAL) {
        ret = -EIO;
        goto out;
    }
    ret = 0;
out:
    vart_memory_region_destroy(&ram);
    return ret;
}

static int check_layout(uint64_t ram_size, uint64_t kernel_size,
                        uint64_t initrd_size, uint64_t initrd_start)
{
    const VartVirtBootLayoutConfig config = {
        .ram_base = UINT64_C(0x80000000),
        .ram_size = ram_size,
        .kernel_size = kernel_size,
        .initrd_size = initrd_size,
    };
    VartVirtBootLayout layout;
    uint64_t ram_end = config.ram_base + config.ram_size;
    int ret;

    ret = vart_virt_boot_layout(&config, &layout);
    if (ret < 0 || layout.kernel_start != config.ram_base ||
        layout.kernel_end != config.ram_base + kernel_size ||
        layout.initrd_start != initrd_start ||
        layout.initrd_end != initrd_start + initrd_size ||
        layout.fdt_start != ram_end - VART_VIRT_FDT_MAX_SIZE) {
        return -EIO;
    }
    return 0;
}

static int check_errors(void)
{
    VartVirtBootLayoutConfig config = {
        .ram_base = UINT64_C(0x80000000),
        .ram_size = UINT64_C(0x10000000),
        .kernel_size = UINT64_C(0x02000000),
        .initrd_size = UINT64_C(0x01000000),
    };
    VartVirtBootLayout layout = {
        .kernel_start = UINT64_MAX,
    };

    if (vart_virt_boot_layout(NULL, &layout) != -EINVAL ||
        vart_virt_boot_layout(&config, NULL) != -EINVAL) {
        return -EIO;
    }
    config.kernel_size = 0;
    if (vart_virt_boot_layout(&config, &layout) != -EINVAL) {
        return -EIO;
    }
    config.kernel_size = UINT64_C(0x02000000);
    config.ram_base = UINT64_MAX - 1;
    if (vart_virt_boot_layout(&config, &layout) != -EOVERFLOW) {
        return -EIO;
    }
    config.ram_base = UINT64_C(0x80000000);
    config.ram_size = VART_VIRT_FDT_MAX_SIZE - 1;
    if (vart_virt_boot_layout(&config, &layout) != -ENOSPC) {
        return -EIO;
    }
    config.ram_size = UINT64_C(0x10000000);
    config.kernel_size = UINT64_C(0x09000000);
    if (vart_virt_boot_layout(&config, &layout) != -ENOSPC) {
        return -EIO;
    }
    config.kernel_size = UINT64_C(0x02000000);
    config.initrd_size = UINT64_C(0x09000000);
    if (vart_virt_boot_layout(&config, &layout) != -ENOSPC ||
        layout.kernel_start != UINT64_MAX) {
        return -EIO;
    }
    return 0;
}

static int check_unaligned_ram(void)
{
    const VartVirtBootLayoutConfig config = {
        .ram_base = UINT64_C(0x80001000),
        .ram_size = UINT64_C(0x10000000),
        .kernel_size = UINT64_C(0x00100000),
    };
    VartVirtBootLayout layout;

    if (vart_virt_boot_layout(&config, &layout) < 0 ||
        layout.kernel_start != UINT64_C(0x80200000) ||
        layout.kernel_end != UINT64_C(0x80300000) ||
        layout.fdt_start != UINT64_C(0x8fe00000)) {
        return -EIO;
    }
    return 0;
}

int main(void)
{
    if (check_layout(UINT64_C(0x40000000), UINT64_C(0x02000000),
                     UINT64_C(0x04000000), UINT64_C(0xa0000000)) < 0 ||
        check_layout(UINT64_C(0x20000000), UINT64_C(0x02000000),
                     UINT64_C(0x02000000), UINT64_C(0x90000000)) < 0 ||
        check_layout(UINT64_C(0x80000000), UINT64_C(0x04000000),
                     UINT64_C(0x08000000), UINT64_C(0xa0000000)) < 0 ||
        check_layout(UINT64_C(0x10000000), UINT64_C(0x02000000),
                     UINT64_C(0x07e00000), UINT64_C(0x88000000)) < 0 ||
        check_layout(UINT64_C(0x10000000), UINT64_C(0x02000000), 0, 0) < 0 ||
        check_unaligned_ram() < 0 ||
        check_errors() < 0 ||
        check_load() < 0) {
        fprintf(stderr, "not ok - place virt boot resources\n");
        return EXIT_FAILURE;
    }
    printf("ok - place virt boot resources\n");
    return EXIT_SUCCESS;
}
