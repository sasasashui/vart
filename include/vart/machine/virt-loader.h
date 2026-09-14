#ifndef VART_MACHINE_VIRT_LOADER_H
#define VART_MACHINE_VIRT_LOADER_H

#include <stddef.h>
#include <stdint.h>

#include "vart/machine/virt-fdt.h"
#include "vart/memory.h"
#include "vart/riscv-cpu.h"

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

typedef struct VartVirtBootConfig {
    const void *kernel;
    size_t kernel_size;
    const void *initrd;
    size_t initrd_size;
    const VartVirtFdtCpu *cpus;
    size_t cpu_count;
    uint32_t timebase_frequency;
    const char *bootargs;
} VartVirtBootConfig;

int vart_virt_boot_layout(const VartVirtBootLayoutConfig *config,
                          VartVirtBootLayout *layout);
/* Load all direct-boot resources and return the matching vCPU boot state. */
int vart_virt_boot_load(VartMemoryRegion *ram,
                        const VartVirtBootConfig *config,
                        VartRiscvBootInfo *boot);

#endif
