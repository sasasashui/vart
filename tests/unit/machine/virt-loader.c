#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "vart/machine/virt-loader.h"

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
        check_errors() < 0) {
        fprintf(stderr, "not ok - place virt boot resources\n");
        return EXIT_FAILURE;
    }
    printf("ok - place virt boot resources\n");
    return EXIT_SUCCESS;
}
