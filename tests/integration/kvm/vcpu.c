#include <errno.h>
#include <asm/kvm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vart/kvm.h"
#include "vart/vcpu.h"
#include "vart/vm.h"

static int fail(const char *operation, int error)
{
    fprintf(stderr, "not ok - %s: %s\n", operation, strerror(-error));
    return EXIT_FAILURE;
}

int main(void)
{
    const uint64_t test_pc = UINT64_C(0x80000000);
    const uint64_t test_a0 = UINT64_C(0x1122334455667788);
    const uint64_t test_a1 = UINT64_C(0x8877665544332211);
    VartVcpu vcpu;
    VartKvm kvm;
    VartVm vm;
    uint64_t value;
    int ret;

    ret = vart_kvm_open(&kvm);
    if (ret < 0) {
        return fail("open KVM", ret);
    }
    ret = vart_vm_create(&vm, &kvm);
    if (ret < 0) {
        vart_kvm_close(&kvm);
        return fail("create VM", ret);
    }
    ret = vart_vcpu_create(&vcpu, &vm, 0);
    if (ret < 0) {
        vart_vm_destroy(&vm);
        vart_kvm_close(&kvm);
        return fail("create vCPU", ret);
    }

    ret = vart_vcpu_set_pc(&vcpu, test_pc);
    if (ret < 0) {
        goto fail_vcpu;
    }
    ret = vart_vcpu_set_mode(&vcpu, KVM_RISCV_MODE_S);
    if (ret < 0 || vart_vcpu_set_mode(&vcpu, 2) != -EINVAL) {
        goto fail_vcpu;
    }
    ret = vart_vcpu_set_gpr(&vcpu, 10, test_a0);
    if (ret < 0) {
        goto fail_vcpu;
    }
    ret = vart_vcpu_set_gpr(&vcpu, 11, test_a1);
    if (ret < 0) {
        goto fail_vcpu;
    }

    ret = vart_vcpu_get_pc(&vcpu, &value);
    if (ret < 0 || value != test_pc) {
        goto fail_value;
    }
    ret = vart_vcpu_get_gpr(&vcpu, 10, &value);
    if (ret < 0 || value != test_a0) {
        goto fail_value;
    }
    ret = vart_vcpu_get_gpr(&vcpu, 11, &value);
    if (ret < 0 || value != test_a1) {
        goto fail_value;
    }
    ret = vart_vcpu_get_gpr(&vcpu, 0, &value);
    if (ret < 0 || value != 0 ||
        vart_vcpu_set_gpr(&vcpu, 0, 1) != -EINVAL ||
        vart_vcpu_get_gpr(&vcpu, 32, &value) != -EINVAL ||
        vart_vcpu_set_gpr(&vcpu, 32, 0) != -EINVAL) {
        goto fail_value;
    }

    vcpu.mmio_read_pending = true;
    vcpu.mmio_read_size = 4;
    ret = vart_vcpu_complete_mmio_read(&vcpu, UINT64_C(0x12345678));
    if (ret < 0 || vcpu.run->mmio.data[0] != 0x78 ||
        vcpu.run->mmio.data[1] != 0x56 || vcpu.run->mmio.data[2] != 0x34 ||
        vcpu.run->mmio.data[3] != 0x12 || vcpu.mmio_read_pending ||
        vart_vcpu_complete_mmio_read(&vcpu, 0) != -EINVAL) {
        goto fail_value;
    }

    vart_vcpu_destroy(&vcpu);
    vart_vm_destroy(&vm);
    vart_kvm_close(&kvm);
    printf("ok - create vCPU and access RISC-V core registers\n");
    return EXIT_SUCCESS;

fail_value:
    ret = ret < 0 ? ret : -EIO;
fail_vcpu:
    vart_vcpu_destroy(&vcpu);
    vart_vm_destroy(&vm);
    vart_kvm_close(&kvm);
    return fail("access vCPU registers", ret);
}
