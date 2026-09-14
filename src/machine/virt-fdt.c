#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vart/fdt.h"
#include "vart/machine/virt-fdt.h"

#define VIRT_FDT_STRUCTURE_CAPACITY 16384
#define VIRT_FDT_STRINGS_CAPACITY 2048

static int add_string_list(VartFdt *fdt, const char *name,
                           const char *const *values, size_t count)
{
    uint8_t *data;
    size_t offset = 0;
    size_t size = 0;
    size_t i;
    int ret;

    for (i = 0; i < count; i++) {
        size_t length = strlen(values[i]) + 1;

        if (length > UINT32_MAX - size) {
            return -EOVERFLOW;
        }
        size += length;
    }
    data = malloc(size);
    if (data == NULL) {
        return -ENOMEM;
    }
    for (i = 0; i < count; i++) {
        size_t length = strlen(values[i]) + 1;

        memcpy(data + offset, values[i], length);
        offset += length;
    }
    ret = vart_fdt_property(fdt, name, data, size);
    free(data);
    return ret;
}

static int add_cpu(VartFdt *fdt, const VartVirtFdtCpu *cpu)
{
    char name[sizeof("cpu@ffffffff")];
    int ret;

    snprintf(name, sizeof(name), "cpu@%" PRIx32, cpu->hartid);
    ret = vart_fdt_begin_node(fdt, name);
    if (ret == 0) {
        ret = vart_fdt_property_string(fdt, "device_type", "cpu");
    }
    if (ret == 0) {
        ret = vart_fdt_property_u32(fdt, "reg", cpu->hartid);
    }
    if (ret == 0) {
        ret = vart_fdt_property_string(fdt, "status", "okay");
    }
    if (ret == 0) {
        ret = vart_fdt_property_string(fdt, "compatible", "riscv");
    }
    if (ret == 0) {
        ret = vart_fdt_property_string(fdt, "riscv,isa", cpu->isa);
    }
    if (ret == 0) {
        ret = vart_fdt_property_string(fdt, "riscv,isa-base",
                                       cpu->isa_base);
    }
    if (ret == 0) {
        ret = add_string_list(fdt, "riscv,isa-extensions",
                              cpu->isa_extensions,
                              cpu->isa_extension_count);
    }
    if (ret == 0) {
        ret = vart_fdt_property_string(fdt, "mmu-type", cpu->mmu_type);
    }
    if (ret == 0) {
        ret = vart_fdt_end_node(fdt);
    }
    return ret;
}

static int add_cpus(VartFdt *fdt, const VartVirtFdtConfig *config)
{
    size_t i;
    int ret;

    ret = vart_fdt_begin_node(fdt, "cpus");
    if (ret == 0) {
        ret = vart_fdt_property_u32(fdt, "#address-cells", 1);
    }
    if (ret == 0) {
        ret = vart_fdt_property_u32(fdt, "#size-cells", 0);
    }
    if (ret == 0) {
        ret = vart_fdt_property_u32(fdt, "timebase-frequency",
                                    config->timebase_frequency);
    }
    for (i = 0; ret == 0 && i < config->cpu_count; i++) {
        ret = add_cpu(fdt, &config->cpus[i]);
    }
    if (ret == 0) {
        ret = vart_fdt_end_node(fdt);
    }
    return ret;
}

static int add_memory(VartFdt *fdt, uint64_t base, uint64_t size)
{
    uint32_t reg[] = {
        base >> 32, base,
        size >> 32, size,
    };
    char name[sizeof("memory@ffffffffffffffff")];
    int ret;

    snprintf(name, sizeof(name), "memory@%" PRIx64, base);
    ret = vart_fdt_begin_node(fdt, name);
    if (ret == 0) {
        ret = vart_fdt_property_string(fdt, "device_type", "memory");
    }
    if (ret == 0) {
        ret = vart_fdt_property_cells(fdt, "reg", reg,
                                      sizeof(reg) / sizeof(reg[0]));
    }
    if (ret == 0) {
        ret = vart_fdt_end_node(fdt);
    }
    return ret;
}

static int validate_config(const VartVirtFdtConfig *config)
{
    size_t i;
    size_t j;

    if (config == NULL || config->cpus == NULL || config->cpu_count == 0 ||
        config->timebase_frequency == 0 || config->ram_size == 0 ||
        config->ram_base > UINT64_MAX - config->ram_size) {
        return -EINVAL;
    }
    for (i = 0; i < config->cpu_count; i++) {
        if (config->cpus[i].isa == NULL || config->cpus[i].isa[0] == '\0' ||
            config->cpus[i].isa_base == NULL ||
            config->cpus[i].isa_base[0] == '\0' ||
            config->cpus[i].isa_extensions == NULL ||
            config->cpus[i].isa_extension_count == 0 ||
            config->cpus[i].mmu_type == NULL ||
            config->cpus[i].mmu_type[0] == '\0') {
            return -EINVAL;
        }
        for (j = 0; j < config->cpus[i].isa_extension_count; j++) {
            if (config->cpus[i].isa_extensions[j] == NULL ||
                config->cpus[i].isa_extensions[j][0] == '\0') {
                return -EINVAL;
            }
        }
        for (j = 0; j < i; j++) {
            if (config->cpus[j].hartid == config->cpus[i].hartid) {
                return -EINVAL;
            }
        }
    }
    return 0;
}

int vart_virt_fdt_build(const VartVirtFdtConfig *config,
                        void **blob, size_t *size)
{
    VartFdt fdt;
    int ret;

    if (blob == NULL || size == NULL) {
        return -EINVAL;
    }
    ret = validate_config(config);
    if (ret < 0) {
        return ret;
    }
    ret = vart_fdt_init(&fdt, VIRT_FDT_STRUCTURE_CAPACITY,
                        VIRT_FDT_STRINGS_CAPACITY);
    if (ret < 0) {
        return ret;
    }
    ret = vart_fdt_begin_node(&fdt, "");
    if (ret == 0) {
        ret = vart_fdt_property_string(&fdt, "model",
                                       "VART RISC-V virtual machine");
    }
    if (ret == 0) {
        ret = vart_fdt_property_string(&fdt, "compatible", "riscv-virtio");
    }
    if (ret == 0) {
        ret = vart_fdt_property_u32(&fdt, "#address-cells", 2);
    }
    if (ret == 0) {
        ret = vart_fdt_property_u32(&fdt, "#size-cells", 2);
    }
    if (ret == 0) {
        ret = add_cpus(&fdt, config);
    }
    if (ret == 0) {
        ret = add_memory(&fdt, config->ram_base, config->ram_size);
    }
    if (ret == 0) {
        ret = vart_fdt_end_node(&fdt);
    }
    if (ret == 0) {
        ret = vart_fdt_finish(&fdt, blob, size);
    }
    vart_fdt_destroy(&fdt);
    return ret;
}
