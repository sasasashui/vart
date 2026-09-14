#ifndef VART_MACHINE_VIRT_FDT_H
#define VART_MACHINE_VIRT_FDT_H

#include <stddef.h>
#include <stdint.h>

typedef struct VartVirtFdtCpu {
    uint32_t hartid;
    const char *isa;
    const char *isa_base;
    const char *const *isa_extensions;
    size_t isa_extension_count;
    const char *mmu_type;
} VartVirtFdtCpu;

typedef struct VartVirtFdtConfig {
    const VartVirtFdtCpu *cpus;
    size_t cpu_count;
    uint32_t timebase_frequency;
    uint64_t ram_base;
    uint64_t ram_size;
} VartVirtFdtConfig;

/* The caller owns the returned blob and releases it with free(). */
int vart_virt_fdt_build(const VartVirtFdtConfig *config,
                        void **blob, size_t *size);

#endif
