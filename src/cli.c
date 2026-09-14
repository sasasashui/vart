#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vart/cli.h"
#include "vart/machine/virt.h"

#define DEFAULT_MEMORY_SIZE (512 * 1024 * 1024UL)
#define DEFAULT_BOOTARGS \
    "console=ttyS0 earlycon=uart8250,mmio,0x10000000"

static int parse_count(const char *text, size_t maximum, size_t *value)
{
    unsigned long long number;
    char *end;

    errno = 0;
    number = strtoull(text, &end, 10);
    if (errno != 0 || text[0] == '\0' || end[0] != '\0' ||
        number == 0 || number > maximum) {
        return -EINVAL;
    }
    *value = (size_t)number;
    return 0;
}

static int parse_memory(const char *text, size_t *value)
{
    unsigned long long multiplier = 1;
    unsigned long long number;
    char *end;

    errno = 0;
    number = strtoull(text, &end, 10);
    if (errno != 0 || text[0] == '\0' || end == text) {
        return -EINVAL;
    }
    if (end[0] != '\0' && end[1] == '\0') {
        switch (end[0]) {
        case 'K': case 'k': multiplier = 1024; break;
        case 'M': case 'm': multiplier = 1024 * 1024; break;
        case 'G': case 'g': multiplier = 1024 * 1024 * 1024ULL; break;
        default: return -EINVAL;
        }
    } else if (end[0] != '\0') {
        return -EINVAL;
    }
    if (number == 0 || number > SIZE_MAX / multiplier) {
        return -ERANGE;
    }
    *value = (size_t)(number * multiplier);
    if (*value < 4 * 1024 * 1024 || (*value & 4095) != 0) {
        return -EINVAL;
    }
    return 0;
}

int vart_cli_parse(int argc, char **argv, VartCliOptions *options)
{
    VartCliOptions result = {
        .mode = VART_CLI_RUN,
        .bootargs = DEFAULT_BOOTARGS,
        .memory_size = DEFAULT_MEMORY_SIZE,
        .vcpu_count = 1,
    };
    int i;

    if (argc < 1 || argv == NULL || options == NULL) {
        return -EINVAL;
    }
    for (i = 1; i < argc; i++) {
        const char *argument = argv[i];

        if (!strcmp(argument, "--probe")) {
            if (argc != 2) {
                return -EINVAL;
            }
            result.mode = VART_CLI_PROBE;
        } else if (!strcmp(argument, "--help") ||
                   !strcmp(argument, "-h")) {
            if (argc != 2) {
                return -EINVAL;
            }
            result.mode = VART_CLI_HELP;
        } else if (!strcmp(argument, "--test-device")) {
            result.enable_test_device = true;
        } else if (!strcmp(argument, "--kernel") ||
                   !strcmp(argument, "--initrd") ||
                   !strcmp(argument, "--append") ||
                   !strcmp(argument, "--memory") ||
                   !strcmp(argument, "--cpus")) {
            const char *value;
            int ret = 0;

            if (++i == argc) {
                return -EINVAL;
            }
            value = argv[i];
            if (!strcmp(argument, "--kernel")) {
                result.kernel_path = value;
            } else if (!strcmp(argument, "--initrd")) {
                result.initrd_path = value;
            } else if (!strcmp(argument, "--append")) {
                result.bootargs = value;
            } else if (!strcmp(argument, "--memory")) {
                ret = parse_memory(value, &result.memory_size);
            } else {
                ret = parse_count(value, VART_VIRT_MAX_CPUS,
                                  &result.vcpu_count);
            }
            if (ret < 0) {
                return ret;
            }
        } else {
            return -EINVAL;
        }
    }
    if (result.mode == VART_CLI_RUN && result.kernel_path == NULL) {
        return -EINVAL;
    }
    *options = result;
    return 0;
}

void vart_cli_usage(const char *program)
{
    fprintf(stderr,
        "Usage: %s --probe\n"
        "       %s --kernel PATH [--initrd PATH] [--append TEXT]\n"
        "          [--memory SIZE] [--cpus COUNT] [--test-device]\n",
        program, program);
}
