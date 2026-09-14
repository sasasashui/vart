#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vart/cli.h"

static int check_defaults(void)
{
    char *argv[] = { "vart", "--kernel", "Image" };
    VartCliOptions options;

    if (vart_cli_parse(3, argv, &options) < 0 ||
        options.mode != VART_CLI_RUN ||
        strcmp(options.kernel_path, "Image") || options.initrd_path != NULL ||
        options.memory_size != 512UL * 1024 * 1024 ||
        options.vcpu_count != 1 || options.enable_test_device ||
        strstr(options.bootargs, "console=ttyS0") == NULL) {
        return -EIO;
    }
    return 0;
}

static int check_options(void)
{
    char *argv[] = {
        "vart", "--kernel", "kernel", "--initrd", "initrd",
        "--append", "panic=-1", "--memory", "2G", "--cpus", "4",
        "--test-device",
    };
    VartCliOptions options;

    if (vart_cli_parse(sizeof(argv) / sizeof(argv[0]), argv, &options) < 0 ||
        strcmp(options.kernel_path, "kernel") ||
        strcmp(options.initrd_path, "initrd") ||
        strcmp(options.bootargs, "panic=-1") ||
        options.memory_size != 2UL * 1024 * 1024 * 1024 ||
        options.vcpu_count != 4 || !options.enable_test_device) {
        return -EIO;
    }
    return 0;
}

static int check_modes(void)
{
    char *probe[] = { "vart", "--probe" };
    char *help[] = { "vart", "--help" };
    VartCliOptions options;

    if (vart_cli_parse(2, probe, &options) < 0 ||
        options.mode != VART_CLI_PROBE ||
        vart_cli_parse(2, help, &options) < 0 ||
        options.mode != VART_CLI_HELP) {
        return -EIO;
    }
    return 0;
}

static int check_errors(void)
{
    char *missing_kernel[] = { "vart", "--memory", "64M" };
    char *missing_value[] = { "vart", "--kernel" };
    char *bad_memory[] = { "vart", "--kernel", "Image",
                           "--memory", "64MB" };
    char *small_memory[] = { "vart", "--kernel", "Image",
                             "--memory", "1M" };
    char *bad_cpus[] = { "vart", "--kernel", "Image", "--cpus", "0" };
    char *mixed_mode[] = { "vart", "--probe", "--kernel", "Image" };
    VartCliOptions options;

    if (vart_cli_parse(3, missing_kernel, &options) != -EINVAL ||
        vart_cli_parse(2, missing_value, &options) != -EINVAL ||
        vart_cli_parse(5, bad_memory, &options) != -EINVAL ||
        vart_cli_parse(5, small_memory, &options) != -EINVAL ||
        vart_cli_parse(5, bad_cpus, &options) != -EINVAL ||
        vart_cli_parse(4, mixed_mode, &options) != -EINVAL ||
        vart_cli_parse(0, NULL, &options) != -EINVAL) {
        return -EIO;
    }
    return 0;
}

int main(void)
{
    if (check_defaults() < 0 || check_options() < 0 || check_modes() < 0 ||
        check_errors() < 0) {
        fprintf(stderr, "not ok - parse VART command-line options\n");
        return EXIT_FAILURE;
    }
    puts("ok - parse VART command-line options");
    return EXIT_SUCCESS;
}
