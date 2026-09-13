#include <errno.h>
#include <linux/kvm.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vart/kvm.h"
#include "vart/riscv-kvm.h"
#include "vart/vcpu.h"
#include "vart/vm.h"

static const char *feature_state(const VartRiscvKvmFeature *feature)
{
    if (!feature->available) {
        return "unavailable";
    }
    return feature->enabled ? "enabled" : "disabled";
}

static int probe_kvm(void)
{
    VartKvm kvm;
    VartRiscvKvmCaps caps;
    VartVcpu vcpu;
    VartVm vm;
    unsigned int i;
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
    printf("MP state: %s\n", kvm.mp_state ? "yes" : "no");
    printf("RISC-V reset MP state: %s\n",
           kvm.riscv_mp_state_reset ? "yes" : "no");

    ret = vart_vm_create(&vm, &kvm);
    if (ret < 0) {
        fprintf(stderr, "vart: cannot create VM: %s\n", strerror(-ret));
        vart_kvm_close(&kvm);
        return EXIT_FAILURE;
    }

    ret = vart_vm_check_device(&vm, KVM_DEV_TYPE_RISCV_AIA);
    printf("RISC-V AIA device: %s\n", ret == 1 ? "yes" : "no");
    if (ret < 0) {
        goto out_vm;
    }

    ret = vart_vcpu_create(&vcpu, &vm, 0);
    if (ret < 0) {
        fprintf(stderr, "vart: cannot create probe vCPU: %s\n",
                strerror(-ret));
        goto out_vm;
    }
    ret = vart_riscv_kvm_probe_vcpu(&vcpu, &caps);
    if (ret < 0) {
        fprintf(stderr, "vart: cannot probe RISC-V KVM: %s\n",
                strerror(-ret));
        goto out_vcpu;
    }

    printf("RISC-V register list: %s\n", caps.reg_list ? "yes" : "no");
    printf("RISC-V core mode register: %s\n",
           caps.core_mode ? "yes" : "no");
    printf("RISC-V timer frequency register: %s\n",
           caps.timer_frequency ? "yes" : "no");
    for (i = 0; i < VART_RISCV_KVM_ISA_COUNT; i++) {
        printf("RISC-V ISA %s: %s\n", vart_riscv_kvm_isa_name(i),
               feature_state(&caps.isa[i]));
    }
    for (i = 0; i < VART_RISCV_KVM_SBI_COUNT; i++) {
        printf("RISC-V SBI %s: %s\n", vart_riscv_kvm_sbi_name(i),
               feature_state(&caps.sbi[i]));
    }

out_vcpu:
    vart_vcpu_destroy(&vcpu);
out_vm:

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
