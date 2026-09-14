#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "vart/fdt.h"

#define FDT_BEGIN_NODE 1
#define FDT_END_NODE 2
#define FDT_PROP 3
#define FDT_END 9
#define FDT_HEADER_SIZE 40
#define FDT_RESERVE_MAP_SIZE 16
#define FDT_VERSION 17
#define FDT_LAST_COMP_VERSION 16

static void write_be32(uint8_t *data, uint32_t value)
{
    data[0] = value >> 24;
    data[1] = value >> 16;
    data[2] = value >> 8;
    data[3] = value;
}

static void write_be64(uint8_t *data, uint64_t value)
{
    write_be32(data, value >> 32);
    write_be32(data + sizeof(uint32_t), (uint32_t)value);
}

static int aligned_size(size_t size, size_t alignment, size_t *result)
{
    size_t mask = alignment - 1;

    if (size > SIZE_MAX - mask) {
        return -EOVERFLOW;
    }
    *result = (size + mask) & ~mask;
    return 0;
}

static int reserve_structure(VartFdt *fdt, size_t size, size_t *padded)
{
    int ret;

    ret = aligned_size(size, 4, padded);
    if (ret < 0 || *padded > fdt->structure_capacity -
                            fdt->structure_size) {
        return ret < 0 ? ret : -ENOSPC;
    }
    return 0;
}

static void append_u32(VartFdt *fdt, uint32_t value)
{
    write_be32(fdt->structure + fdt->structure_size, value);
    fdt->structure_size += sizeof(value);
}

static int find_string(const VartFdt *fdt, const char *string,
                       uint32_t *offset)
{
    size_t cursor = 0;

    while (cursor < fdt->strings_size) {
        if (strcmp(fdt->strings + cursor, string) == 0) {
            *offset = (uint32_t)cursor;
            return 1;
        }
        cursor += strlen(fdt->strings + cursor) + 1;
    }
    return 0;
}

int vart_fdt_init(VartFdt *fdt, size_t structure_capacity,
                  size_t strings_capacity)
{
    if (fdt == NULL || structure_capacity == 0 || strings_capacity == 0) {
        return -EINVAL;
    }
    if (structure_capacity > UINT32_MAX || strings_capacity > UINT32_MAX) {
        return -EOVERFLOW;
    }
    memset(fdt, 0, sizeof(*fdt));
    fdt->structure = calloc(1, structure_capacity);
    fdt->strings = calloc(1, strings_capacity);
    fdt->depth_capacity = structure_capacity / 8 + 1;
    fdt->child_seen = calloc(fdt->depth_capacity,
                             sizeof(*fdt->child_seen));
    if (fdt->structure == NULL || fdt->strings == NULL ||
        fdt->child_seen == NULL) {
        vart_fdt_destroy(fdt);
        return -ENOMEM;
    }
    fdt->structure_capacity = structure_capacity;
    fdt->strings_capacity = strings_capacity;
    return 0;
}

void vart_fdt_destroy(VartFdt *fdt)
{
    if (fdt == NULL) {
        return;
    }
    free(fdt->structure);
    free(fdt->strings);
    free(fdt->child_seen);
    memset(fdt, 0, sizeof(*fdt));
}

int vart_fdt_begin_node(VartFdt *fdt, const char *name)
{
    size_t name_size;
    size_t padded;
    int ret;

    if (fdt == NULL || name == NULL || fdt->finished || fdt->root_closed ||
        (!fdt->root_seen && *name != '\0') ||
        (fdt->root_seen && *name == '\0') || strchr(name, '/') != NULL) {
        return -EINVAL;
    }
    if (fdt->depth >= fdt->depth_capacity) {
        return -EOVERFLOW;
    }
    name_size = strlen(name) + 1;
    if (name_size > SIZE_MAX - sizeof(uint32_t)) {
        return -EOVERFLOW;
    }
    ret = reserve_structure(fdt, sizeof(uint32_t) + name_size, &padded);
    if (ret < 0) {
        return ret;
    }
    append_u32(fdt, FDT_BEGIN_NODE);
    memcpy(fdt->structure + fdt->structure_size, name, name_size);
    fdt->structure_size += name_size;
    fdt->structure_size = (fdt->structure_size + 3) & ~(size_t)3;
    if (fdt->depth != 0) {
        fdt->child_seen[fdt->depth - 1] = true;
    }
    fdt->child_seen[fdt->depth] = false;
    fdt->depth++;
    fdt->root_seen = true;
    return 0;
}

int vart_fdt_end_node(VartFdt *fdt)
{
    size_t padded;
    int ret;

    if (fdt == NULL || fdt->finished || fdt->depth == 0) {
        return -EINVAL;
    }
    ret = reserve_structure(fdt, sizeof(uint32_t), &padded);
    if (ret < 0) {
        return ret;
    }
    append_u32(fdt, FDT_END_NODE);
    fdt->depth--;
    if (fdt->depth == 0) {
        fdt->root_closed = true;
    }
    return 0;
}

int vart_fdt_property(VartFdt *fdt, const char *name,
                      const void *data, size_t size)
{
    size_t string_size;
    size_t padded;
    uint32_t name_offset;
    int found;
    int ret;

    if (fdt == NULL || name == NULL || *name == '\0' ||
        (data == NULL && size != 0) ||
        fdt->finished || fdt->depth == 0 ||
        fdt->child_seen[fdt->depth - 1]) {
        return -EINVAL;
    }
    if (size > UINT32_MAX) {
        return -EOVERFLOW;
    }
    if (size > SIZE_MAX - 3 * sizeof(uint32_t)) {
        return -EOVERFLOW;
    }
    ret = reserve_structure(fdt, 3 * sizeof(uint32_t) + size, &padded);
    if (ret < 0) {
        return ret;
    }

    found = find_string(fdt, name, &name_offset);
    if (!found) {
        string_size = strlen(name) + 1;
        if (string_size > fdt->strings_capacity - fdt->strings_size) {
            return -ENOSPC;
        }
        name_offset = (uint32_t)fdt->strings_size;
        memcpy(fdt->strings + fdt->strings_size, name, string_size);
        fdt->strings_size += string_size;
    }

    append_u32(fdt, FDT_PROP);
    append_u32(fdt, (uint32_t)size);
    append_u32(fdt, name_offset);
    if (size != 0) {
        memcpy(fdt->structure + fdt->structure_size, data, size);
    }
    fdt->structure_size += size;
    fdt->structure_size = (fdt->structure_size + 3) & ~(size_t)3;
    return 0;
}

int vart_fdt_property_string(VartFdt *fdt, const char *name,
                             const char *value)
{
    if (value == NULL) {
        return -EINVAL;
    }
    return vart_fdt_property(fdt, name, value, strlen(value) + 1);
}

int vart_fdt_property_u32(VartFdt *fdt, const char *name, uint32_t value)
{
    uint8_t data[sizeof(value)];

    write_be32(data, value);
    return vart_fdt_property(fdt, name, data, sizeof(data));
}

int vart_fdt_property_u64(VartFdt *fdt, const char *name, uint64_t value)
{
    uint8_t data[sizeof(value)];

    write_be64(data, value);
    return vart_fdt_property(fdt, name, data, sizeof(data));
}

int vart_fdt_property_cells(VartFdt *fdt, const char *name,
                            const uint32_t *cells, size_t count)
{
    uint8_t *data;
    size_t size;
    size_t i;
    int ret;

    if (cells == NULL && count != 0) {
        return -EINVAL;
    }
    if (count > UINT32_MAX / sizeof(*cells)) {
        return -EOVERFLOW;
    }
    size = count * sizeof(*cells);
    if (size == 0) {
        return vart_fdt_property(fdt, name, NULL, 0);
    }
    data = malloc(size);
    if (data == NULL) {
        return -ENOMEM;
    }
    for (i = 0; i < count; i++) {
        write_be32(data + i * sizeof(*cells), cells[i]);
    }
    ret = vart_fdt_property(fdt, name, data, size);
    free(data);
    return ret;
}

static void write_header_u32(uint8_t *blob, size_t offset, uint32_t value)
{
    write_be32(blob + offset, value);
}

int vart_fdt_finish(VartFdt *fdt, void **blob, size_t *size)
{
    const size_t structure_offset = FDT_HEADER_SIZE + FDT_RESERVE_MAP_SIZE;
    size_t strings_offset;
    size_t total_size;
    size_t padded;
    uint8_t *result;
    int ret;

    if (fdt == NULL || blob == NULL || size == NULL || fdt->finished ||
        !fdt->root_seen || !fdt->root_closed || fdt->depth != 0) {
        return -EINVAL;
    }
    ret = reserve_structure(fdt, sizeof(uint32_t), &padded);
    if (ret < 0) {
        return ret;
    }
    if (structure_offset > SIZE_MAX - fdt->structure_size -
                           sizeof(uint32_t)) {
        return -EOVERFLOW;
    }
    strings_offset = structure_offset + fdt->structure_size +
                     sizeof(uint32_t);
    if (strings_offset > UINT32_MAX ||
        fdt->strings_size > UINT32_MAX - strings_offset) {
        return -EOVERFLOW;
    }
    total_size = strings_offset + fdt->strings_size;
    result = calloc(1, total_size);
    if (result == NULL) {
        return -ENOMEM;
    }

    append_u32(fdt, FDT_END);
    memcpy(result + structure_offset, fdt->structure, fdt->structure_size);
    memcpy(result + strings_offset, fdt->strings, fdt->strings_size);
    write_header_u32(result, 0, VART_FDT_MAGIC);
    write_header_u32(result, 4, (uint32_t)total_size);
    write_header_u32(result, 8, (uint32_t)structure_offset);
    write_header_u32(result, 12, (uint32_t)strings_offset);
    write_header_u32(result, 16, FDT_HEADER_SIZE);
    write_header_u32(result, 20, FDT_VERSION);
    write_header_u32(result, 24, FDT_LAST_COMP_VERSION);
    write_header_u32(result, 28, 0);
    write_header_u32(result, 32, (uint32_t)fdt->strings_size);
    write_header_u32(result, 36, (uint32_t)fdt->structure_size);

    fdt->finished = true;
    *blob = result;
    *size = total_size;
    return 0;
}
