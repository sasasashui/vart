#include <errno.h>
#include <linux/kvm.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vart/kvm.h"
#include "vart/vm.h"

static int probe_kvm(void)
{
    VartKvm kvm;
    VartVm vm;
    int ret;

    ret = vart_kvm_open(&kvm);
    if (ret < 0) {
        fprintf(stderr, "vart: cannot initialize KVM: %s\n", strerror(-ret));
        return EXIT_FAILURE;
    }

    printf("KVM API version: %d\n", kvm.api_version);
    printf("vCPU mmap size: %d bytes\n", kvm.vcpu_mmap_size);
    printf("recommended vCPUs: %d\n", kvm.recommended_vcpus);
    printf("maximum vCPUs: %d\n", kvm.max_vcpus);
    printf("user memory: %s\n", kvm.user_memory ? "yes" : "no");
    printf("one-reg API: %s\n", kvm.one_reg ? "yes" : "no");
    printf("irqfd: %s\n", kvm.irqfd ? "yes" : "no");
    printf("ioeventfd: %s\n", kvm.ioeventfd ? "yes" : "no");
    printf("immediate exit: %s\n", kvm.immediate_exit ? "yes" : "no");

    ret = vart_vm_create(&vm, &kvm);
    if (ret < 0) {
        fprintf(stderr, "vart: cannot create VM: %s\n", strerror(-ret));
        vart_kvm_close(&kvm);
        return EXIT_FAILURE;
    }

    ret = vart_vm_check_device(&vm, KVM_DEV_TYPE_RISCV_AIA);
    printf("RISC-V AIA device: %s\n", ret == 1 ? "yes" : "no");

    vart_vm_destroy(&vm);
    vart_kvm_close(&kvm);
    return ret < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s --probe\n", program);
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--probe") == 0) {
        return probe_kvm();
    }

    usage(argv[0]);
    return EXIT_FAILURE;
}
