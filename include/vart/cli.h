#ifndef VART_CLI_H
#define VART_CLI_H

#include <stdbool.h>
#include <stddef.h>

typedef enum VartCliMode {
    VART_CLI_RUN,
    VART_CLI_PROBE,
    VART_CLI_HELP,
} VartCliMode;

typedef struct VartCliOptions {
    VartCliMode mode;
    const char *kernel_path;
    const char *initrd_path;
    const char *bootargs;
    const char *fdt_dump_path;
    const char *virtio_blk_path;
    const char *virtio_net_tap;
    int virtio_net_fd;
    bool virtio_net_fd_set;
    size_t memory_size;
    size_t vcpu_count;
    bool enable_test_device;
    bool enable_pcie_switch;
    bool enable_riscv_iommu;
} VartCliOptions;

int vart_cli_parse(int argc, char **argv, VartCliOptions *options);
void vart_cli_usage(const char *program);

#endif
