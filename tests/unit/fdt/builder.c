#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vart/fdt.h"

#define FDT_BEGIN_NODE 1
#define FDT_END_NODE 2
#define FDT_PROP 3
#define FDT_END 9
#define HEADER_SIZE 40

static uint32_t read_be32(const uint8_t *data)
{
    return (uint32_t)data[0] << 24 | (uint32_t)data[1] << 16 |
           (uint32_t)data[2] << 8 | data[3];
}

static uint64_t read_be64(const uint8_t *data)
{
    return (uint64_t)read_be32(data) << 32 | read_be32(data + 4);
}

static const uint8_t *skip_name(const uint8_t *cursor)
{
    cursor += strlen((const char *)cursor) + 1;
    return (const uint8_t *)(((uintptr_t)cursor + 3) & ~(uintptr_t)3);
}

static const uint8_t *check_property(const uint8_t *cursor,
                                     const char *strings,
                                     const char *name, uint32_t size,
                                     const uint8_t **data,
                                     uint32_t *name_offset)
{
    uint32_t actual_size;

    if (read_be32(cursor) != FDT_PROP) {
        return NULL;
    }
    actual_size = read_be32(cursor + 4);
    *name_offset = read_be32(cursor + 8);
    if (actual_size != size ||
        strcmp(strings + *name_offset, name) != 0) {
        return NULL;
    }
    *data = cursor + 12;
    cursor += 12 + actual_size;
    return (const uint8_t *)(((uintptr_t)cursor + 3) & ~(uintptr_t)3);
}

static int validate_blob(const void *blob, size_t size)
{
    const uint8_t *bytes = blob;
    const uint8_t *cursor;
    const uint8_t *data;
    const char *strings;
    uint32_t compatible_offset;
    uint32_t duplicate_offset;
    uint32_t name_offset;
    uint32_t structure_offset;
    uint32_t strings_offset;

    if (read_be32(bytes) != VART_FDT_MAGIC ||
        read_be32(bytes + 4) != size || read_be32(bytes + 16) != HEADER_SIZE ||
        read_be32(bytes + 20) != 17 || read_be32(bytes + 24) != 16 ||
        read_be32(bytes + 28) != 0 || read_be32(bytes + 8) % 8 != 0) {
        return -EIO;
    }
    structure_offset = read_be32(bytes + 8);
    strings_offset = read_be32(bytes + 12);
    if (structure_offset + read_be32(bytes + 36) != strings_offset ||
        strings_offset + read_be32(bytes + 32) != size) {
        return -EIO;
    }
    strings = (const char *)bytes + strings_offset;
    cursor = bytes + structure_offset;

    if (read_be32(cursor) != FDT_BEGIN_NODE || cursor[4] != '\0') {
        return -EIO;
    }
    cursor = skip_name(cursor + 4);
    cursor = check_property(cursor, strings, "compatible", 10, &data,
                            &compatible_offset);
    if (cursor == NULL || strcmp((const char *)data, "vart,test") != 0) {
        return -EIO;
    }
    cursor = check_property(cursor, strings, "#address-cells", 4, &data,
                            &name_offset);
    if (cursor == NULL || read_be32(data) != 2) {
        return -EIO;
    }
    cursor = check_property(cursor, strings, "#size-cells", 4, &data,
                            &name_offset);
    if (cursor == NULL || read_be32(data) != 0) {
        return -EIO;
    }
    cursor = check_property(cursor, strings, "vart,test-cells", 12, &data,
                            &name_offset);
    if (cursor == NULL || read_be32(data) != 1 ||
        read_be32(data + 4) != 2 || read_be32(data + 8) != 3) {
        return -EIO;
    }
    cursor = check_property(cursor, strings, "empty-property", 0, &data,
                            &name_offset);
    if (cursor == NULL || read_be32(cursor) != FDT_BEGIN_NODE ||
        strcmp((const char *)cursor + 4,
               "bus@123456789abcdef0") != 0) {
        return -EIO;
    }
    cursor = skip_name(cursor + 4);
    cursor = check_property(cursor, strings, "compatible", 11, &data,
                            &duplicate_offset);
    if (cursor == NULL || strcmp((const char *)data, "simple-bus") != 0 ||
        duplicate_offset != compatible_offset) {
        return -EIO;
    }
    cursor = check_property(cursor, strings, "reg", 8, &data,
                            &name_offset);
    if (cursor == NULL || read_be64(data) != UINT64_C(0x123456789abcdef0) ||
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
    VartFdt fdt;
    void *blob;
    size_t size;
    int ret;

    if (vart_fdt_init(NULL, 64, 64) != -EINVAL ||
        vart_fdt_init(&fdt, 0, 64) != -EINVAL) {
        return -EIO;
    }
    ret = vart_fdt_init(&fdt, 8, 4);
    if (ret < 0) {
        return ret;
    }
    if (vart_fdt_property_u32(&fdt, "before-root", 1) != -EINVAL ||
        vart_fdt_begin_node(&fdt, "not-root") != -EINVAL ||
        vart_fdt_begin_node(&fdt, "") != 0 ||
        vart_fdt_property(&fdt, "long-name", NULL, 0) != -ENOSPC ||
        vart_fdt_end_node(&fdt) != -ENOSPC ||
        vart_fdt_finish(&fdt, &blob, &size) != -EINVAL) {
        vart_fdt_destroy(&fdt);
        return -EIO;
    }
    vart_fdt_destroy(&fdt);
    return 0;
}

int main(int argc, char **argv)
{
    static const uint32_t cells[] = { 1, 2, 3 };
    VartFdt fdt;
    void *blob = NULL;
    size_t size;
    int ret;

    if (argc != 2 || check_errors() < 0) {
        goto fail;
    }
    ret = vart_fdt_init(&fdt, 512, 128);
    if (ret < 0) {
        goto fail;
    }
    ret = vart_fdt_begin_node(&fdt, "");
    if (ret == 0) {
        ret = vart_fdt_property_string(&fdt, "compatible", "vart,test");
    }
    if (ret == 0) {
        ret = vart_fdt_property_u32(&fdt, "#address-cells", 2);
    }
    if (ret == 0) {
        ret = vart_fdt_property_u32(&fdt, "#size-cells", 0);
    }
    if (ret == 0) {
        ret = vart_fdt_property_cells(&fdt, "vart,test-cells", cells,
                                      sizeof(cells) / sizeof(cells[0]));
    }
    if (ret == 0) {
        ret = vart_fdt_property(&fdt, "empty-property", NULL, 0);
    }
    if (ret == 0) {
        ret = vart_fdt_begin_node(&fdt, "bus@123456789abcdef0");
    }
    if (ret == 0) {
        ret = vart_fdt_property_string(&fdt, "compatible", "simple-bus");
    }
    if (ret == 0) {
        ret = vart_fdt_property_u64(&fdt, "reg",
                                    UINT64_C(0x123456789abcdef0));
    }
    if (ret == 0) {
        ret = vart_fdt_end_node(&fdt);
    }
    if (ret == 0 && vart_fdt_property(&fdt, "late-property", NULL, 0) !=
                    -EINVAL) {
        ret = -EIO;
    }
    if (ret == 0) {
        ret = vart_fdt_end_node(&fdt);
    }
    if (ret == 0) {
        ret = vart_fdt_finish(&fdt, &blob, &size);
    }
    if (ret == 0) {
        ret = validate_blob(blob, size);
    }
    if (ret == 0) {
        ret = write_blob(argv[1], blob, size);
    }
    if (ret == 0 &&
        (vart_fdt_finish(&fdt, &blob, &size) != -EINVAL ||
         vart_fdt_begin_node(&fdt, "late") != -EINVAL)) {
        ret = -EIO;
    }
    free(blob);
    vart_fdt_destroy(&fdt);
    if (ret < 0) {
        goto fail;
    }

    printf("ok - build a flattened device tree\n");
    return EXIT_SUCCESS;

fail:
    fprintf(stderr, "not ok - build a flattened device tree\n");
    return EXIT_FAILURE;
}
