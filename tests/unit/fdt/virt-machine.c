#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vart/fdt.h"
#include "vart/machine/virt-fdt.h"

#define FDT_BEGIN_NODE 1
#define FDT_END_NODE 2
#define FDT_PROP 3
#define FDT_END 9

static uint32_t read_be32(const uint8_t *data)
{
    return (uint32_t)data[0] << 24 | (uint32_t)data[1] << 16 |
           (uint32_t)data[2] << 8 | data[3];
}

static const uint8_t *skip_name(const uint8_t *cursor)
{
    cursor += strlen((const char *)cursor) + 1;
    return (const uint8_t *)(((uintptr_t)cursor + 3) & ~(uintptr_t)3);
}

static const uint8_t *check_node(const uint8_t *cursor, const char *name)
{
    if (cursor == NULL || read_be32(cursor) != FDT_BEGIN_NODE ||
        strcmp((const char *)cursor + 4, name) != 0) {
        return NULL;
    }
    return skip_name(cursor + 4);
}

static const uint8_t *check_property(const uint8_t *cursor,
                                     const char *strings,
                                     const char *name, uint32_t size,
                                     const uint8_t **data)
{
    uint32_t actual_size;
    uint32_t name_offset;

    if (cursor == NULL || read_be32(cursor) != FDT_PROP) {
        return NULL;
    }
    actual_size = read_be32(cursor + 4);
    name_offset = read_be32(cursor + 8);
    if (actual_size != size || strcmp(strings + name_offset, name) != 0) {
        return NULL;
    }
    *data = cursor + 12;
    cursor += 12 + actual_size;
    return (const uint8_t *)(((uintptr_t)cursor + 3) & ~(uintptr_t)3);
}

static const uint8_t *check_string(const uint8_t *cursor,
                                   const char *strings,
                                   const char *name, const char *value)
{
    const uint8_t *data;

    cursor = check_property(cursor, strings, name, strlen(value) + 1,
                            &data);
    if (cursor == NULL || strcmp((const char *)data, value) != 0) {
        return NULL;
    }
    return cursor;
}

static const uint8_t *check_u32(const uint8_t *cursor, const char *strings,
                                const char *name, uint32_t value)
{
    const uint8_t *data;

    cursor = check_property(cursor, strings, name, sizeof(value), &data);
    if (cursor == NULL || read_be32(data) != value) {
        return NULL;
    }
    return cursor;
}

static const uint8_t *check_cpu(const uint8_t *cursor, const char *strings,
                                const char *node_name, uint32_t hartid,
                                const char *isa, const char *mmu_type)
{
    static const uint8_t extensions[] =
        "i\0m\0a\0f\0d\0c\0sstc\0ssaia\0zicsr\0";
    const uint8_t *data;

    cursor = check_node(cursor, node_name);
    cursor = check_string(cursor, strings, "device_type", "cpu");
    cursor = check_u32(cursor, strings, "reg", hartid);
    cursor = check_string(cursor, strings, "status", "okay");
    cursor = check_string(cursor, strings, "compatible", "riscv");
    cursor = check_string(cursor, strings, "riscv,isa", isa);
    cursor = check_string(cursor, strings, "riscv,isa-base", "rv64i");
    cursor = check_property(cursor, strings, "riscv,isa-extensions",
                            sizeof(extensions) - 1, &data);
    if (cursor == NULL || memcmp(data, extensions, sizeof(extensions) - 1)) {
        return NULL;
    }
    cursor = check_string(cursor, strings, "mmu-type", mmu_type);
    if (cursor == NULL || read_be32(cursor) != FDT_END_NODE) {
        return NULL;
    }
    return cursor + 4;
}

static int validate_blob(const void *blob, size_t size)
{
    const uint8_t *bytes = blob;
    const uint8_t *cursor;
    const uint8_t *data;
    const char *strings;
    uint32_t strings_offset;

    if (read_be32(bytes) != VART_FDT_MAGIC || read_be32(bytes + 4) != size) {
        return -EIO;
    }
    strings_offset = read_be32(bytes + 12);
    strings = (const char *)bytes + strings_offset;
    cursor = bytes + read_be32(bytes + 8);

    cursor = check_node(cursor, "");
    cursor = check_string(cursor, strings, "model",
                          "VART RISC-V virtual machine");
    cursor = check_string(cursor, strings, "compatible", "riscv-virtio");
    cursor = check_u32(cursor, strings, "#address-cells", 2);
    cursor = check_u32(cursor, strings, "#size-cells", 2);
    cursor = check_node(cursor, "cpus");
    cursor = check_u32(cursor, strings, "#address-cells", 1);
    cursor = check_u32(cursor, strings, "#size-cells", 0);
    cursor = check_u32(cursor, strings, "timebase-frequency", 10000000);
    cursor = check_cpu(cursor, strings, "cpu@0", 0, "rv64imafdc_zicsr",
                       "riscv,sv48");
    cursor = check_cpu(cursor, strings, "cpu@5", 5, "rv64imafdc_zicsr",
                       "riscv,sv39");
    if (cursor == NULL || read_be32(cursor) != FDT_END_NODE) {
        return -EIO;
    }
    cursor = check_node(cursor + 4, "memory@80000000");
    cursor = check_string(cursor, strings, "device_type", "memory");
    cursor = check_property(cursor, strings, "reg", 16, &data);
    if (cursor == NULL || read_be32(data) != 0 ||
        read_be32(data + 4) != UINT32_C(0x80000000) ||
        read_be32(data + 8) != 1 || read_be32(data + 12) != 0 ||
        read_be32(cursor) != FDT_END_NODE ||
        read_be32(cursor + 4) != FDT_END_NODE ||
        read_be32(cursor + 8) != FDT_END) {
        return -EIO;
    }
    return 0;
}

static int write_blob(const char *path, const void *blob, size_t size)
{
    FILE *file;
    int ret = 0;

    file = fopen(path, "wb");
    if (file == NULL) {
        return -errno;
    }
    if (fwrite(blob, size, 1, file) != 1) {
        ret = -EIO;
    }
    if (fclose(file) != 0) {
        ret = -EIO;
    }
    return ret;
}

static int check_errors(void)
{
    static const char *const extensions[] = { "i", "m", "a" };
    VartVirtFdtCpu cpus[] = {
        {
            .hartid = 0,
            .isa = "rv64ima",
            .isa_base = "rv64i",
            .isa_extensions = extensions,
            .isa_extension_count = sizeof(extensions) / sizeof(extensions[0]),
            .mmu_type = "riscv,sv39",
        },
        {
            .hartid = 0,
            .isa = "rv64ima",
            .isa_base = "rv64i",
            .isa_extensions = extensions,
            .isa_extension_count = sizeof(extensions) / sizeof(extensions[0]),
            .mmu_type = "riscv,sv39",
        },
    };
    VartVirtFdtConfig config = {
        .cpus = cpus,
        .cpu_count = 1,
        .timebase_frequency = 10000000,
        .ram_base = UINT64_C(0x80000000),
        .ram_size = UINT64_C(0x10000000),
    };
    void *blob;
    size_t size;

    if (vart_virt_fdt_build(NULL, &blob, &size) != -EINVAL ||
        vart_virt_fdt_build(&config, NULL, &size) != -EINVAL) {
        return -EIO;
    }
    config.cpu_count = 0;
    if (vart_virt_fdt_build(&config, &blob, &size) != -EINVAL) {
        return -EIO;
    }
    config.cpu_count = 1;
    config.ram_size = 0;
    if (vart_virt_fdt_build(&config, &blob, &size) != -EINVAL) {
        return -EIO;
    }
    config.ram_base = UINT64_MAX;
    config.ram_size = 2;
    if (vart_virt_fdt_build(&config, &blob, &size) != -EINVAL) {
        return -EIO;
    }
    config.ram_base = UINT64_C(0x80000000);
    config.ram_size = UINT64_C(0x10000000);
    cpus[0].isa_base = NULL;
    if (vart_virt_fdt_build(&config, &blob, &size) != -EINVAL) {
        return -EIO;
    }
    cpus[0].isa_base = "rv64i";
    cpus[0].isa_extension_count = 0;
    if (vart_virt_fdt_build(&config, &blob, &size) != -EINVAL) {
        return -EIO;
    }
    cpus[0].isa_extension_count =
        sizeof(extensions) / sizeof(extensions[0]);
    config.cpu_count = 2;
    if (vart_virt_fdt_build(&config, &blob, &size) != -EINVAL) {
        return -EIO;
    }
    return 0;
}

int main(int argc, char **argv)
{
    static const char *const extensions[] = {
        "i", "m", "a", "f", "d", "c", "sstc", "ssaia", "zicsr",
    };
    const VartVirtFdtCpu cpus[] = {
        {
            .hartid = 0,
            .isa = "rv64imafdc_zicsr",
            .isa_base = "rv64i",
            .isa_extensions = extensions,
            .isa_extension_count = sizeof(extensions) / sizeof(extensions[0]),
            .mmu_type = "riscv,sv48",
        },
        {
            .hartid = 5,
            .isa = "rv64imafdc_zicsr",
            .isa_base = "rv64i",
            .isa_extensions = extensions,
            .isa_extension_count = sizeof(extensions) / sizeof(extensions[0]),
            .mmu_type = "riscv,sv39",
        },
    };
    const VartVirtFdtConfig config = {
        .cpus = cpus,
        .cpu_count = sizeof(cpus) / sizeof(cpus[0]),
        .timebase_frequency = 10000000,
        .ram_base = UINT64_C(0x80000000),
        .ram_size = UINT64_C(0x100000000),
    };
    void *blob = NULL;
    size_t size;
    int ret;

    if (argc != 2 || check_errors() < 0) {
        goto fail;
    }
    ret = vart_virt_fdt_build(&config, &blob, &size);
    if (ret == 0) {
        ret = validate_blob(blob, size);
    }
    if (ret == 0) {
        ret = write_blob(argv[1], blob, size);
    }
    free(blob);
    if (ret < 0) {
        goto fail;
    }
    printf("ok - build virt CPU and memory device tree\n");
    return EXIT_SUCCESS;

fail:
    fprintf(stderr, "not ok - build virt CPU and memory device tree\n");
    return EXIT_FAILURE;
}
