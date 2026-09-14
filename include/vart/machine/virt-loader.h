#ifndef VART_MACHINE_VIRT_LOADER_H
#define VART_MACHINE_VIRT_LOADER_H

#include <stdint.h>

#define VART_VIRT_BOOT_ALIGN UINT64_C(0x00200000)
#define VART_VIRT_INITRD_MAX_OFFSET UINT64_C(0x20000000)
#define VART_VIRT_FDT_MAX_SIZE UINT64_C(0x00200000)

typedef struct VartVirtBootLayoutConfig {
    uint64_t ram_base;
    uint64_t ram_size;
    uint64_t kernel_size;
    uint64_t initrd_size;
} VartVirtBootLayoutConfig;

typedef struct VartVirtBootLayout {
    uint64_t kernel_start;
    uint64_t kernel_end;
    uint64_t initrd_start;
    uint64_t initrd_end;
    uint64_t fdt_start;
} VartVirtBootLayout;

int vart_virt_boot_layout(const VartVirtBootLayoutConfig *config,
                          VartVirtBootLayout *layout);

#endif
