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
    size_t memory_size;
    size_t vcpu_count;
    bool enable_test_device;
} VartCliOptions;

int vart_cli_parse(int argc, char **argv, VartCliOptions *options);
void vart_cli_usage(const char *program);

#endif
