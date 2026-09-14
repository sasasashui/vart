#ifndef VART_MACHINE_VIRT_MACHINE_LOADER_H
#define VART_MACHINE_VIRT_MACHINE_LOADER_H

#include <stddef.h>
#include <stdint.h>

#include "vart/machine/virt-machine.h"

typedef struct VartVirtMachineBootFiles {
    const char *kernel_path;
    const char *initrd_path;
    const char *bootargs;
    uint32_t timebase_frequency;
    const char *isa;
    const char *isa_base;
    const char *const *isa_extensions;
    size_t isa_extension_count;
    const char *mmu_type;
} VartVirtMachineBootFiles;

/*
 * Load boot files, build the matching FDT, and initialize every vCPU.
 * Guest RAM and vCPU boot state are changed only after all files are read.
 */
int vart_virt_machine_load_boot_files(
    VartVirtMachine *machine, const VartVirtMachineBootFiles *files,
    VartRiscvBootInfo *boot);

#endif
