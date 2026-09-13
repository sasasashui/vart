#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/kvm.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int probe_kvm(void)
{
    int kvm_fd = open("/dev/kvm", O_RDWR | O_CLOEXEC);
    if (kvm_fd < 0) {
        fprintf(stderr, "rvmm: cannot open /dev/kvm: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    int api_version = ioctl(kvm_fd, KVM_GET_API_VERSION, 0);
    if (api_version < 0) {
        fprintf(stderr, "rvmm: KVM_GET_API_VERSION failed: %s\n",
                strerror(errno));
        close(kvm_fd);
        return EXIT_FAILURE;
    }

    printf("KVM API version: %d\n", api_version);
    printf("host architecture: riscv64\n");

    if (api_version != KVM_API_VERSION) {
        fprintf(stderr, "rvmm: expected KVM API version %d\n", KVM_API_VERSION);
        close(kvm_fd);
        return EXIT_FAILURE;
    }

    close(kvm_fd);
    return EXIT_SUCCESS;
}

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s --probe\n", program);
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--probe") == 0)
        return probe_kvm();

    usage(argv[0]);
    return EXIT_FAILURE;
}

