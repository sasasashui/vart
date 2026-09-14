#ifndef VART_FDT_H
#define VART_FDT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VART_FDT_MAGIC UINT32_C(0xd00dfeed)

typedef struct VartFdt {
    uint8_t *structure;
    size_t structure_size;
    size_t structure_capacity;
    char *strings;
    size_t strings_size;
    size_t strings_capacity;
    bool *child_seen;
    size_t depth_capacity;
    unsigned int depth;
    bool root_seen;
    bool root_closed;
    bool finished;
} VartFdt;

int vart_fdt_init(VartFdt *fdt, size_t structure_capacity,
                  size_t strings_capacity);
void vart_fdt_destroy(VartFdt *fdt);
int vart_fdt_begin_node(VartFdt *fdt, const char *name);
int vart_fdt_end_node(VartFdt *fdt);
int vart_fdt_property(VartFdt *fdt, const char *name,
                      const void *data, size_t size);
int vart_fdt_property_string(VartFdt *fdt, const char *name,
                             const char *value);
int vart_fdt_property_u32(VartFdt *fdt, const char *name, uint32_t value);
int vart_fdt_property_u64(VartFdt *fdt, const char *name, uint64_t value);
int vart_fdt_property_cells(VartFdt *fdt, const char *name,
                            const uint32_t *cells, size_t count);

/* The caller owns the returned blob and releases it with free(). */
int vart_fdt_finish(VartFdt *fdt, void **blob, size_t *size);

#endif
