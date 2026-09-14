#include <asm/kvm.h>
#include <errno.h>
#include <linux/kvm.h>
#include <stdio.h>
#include <stdlib.h>

#include "vart/kvm.h"
#include "vart/riscv-aia.h"
#include "vart/vm.h"

int main(void)
{
    VartRiscvAia aia;
    VartKvm kvm;
    VartVm vm;
    int ret;

    ret = vart_kvm_open(&kvm);
    if (ret < 0) {
        return EXIT_FAILURE;
    }
    ret = vart_vm_create(&vm, &kvm);
    if (ret < 0) {
        goto fail_vm;
    }
    if (vart_riscv_aia_create(NULL, &vm,
                              KVM_DEV_RISCV_AIA_MODE_AUTO) != -EINVAL ||
        vart_riscv_aia_create(&aia, NULL,
                              KVM_DEV_RISCV_AIA_MODE_AUTO) != -EINVAL ||
        vart_riscv_aia_create(&aia, &vm,
                              KVM_DEV_RISCV_AIA_MODE_AUTO + 1) != -EINVAL) {
        ret = -EINVAL;
        goto fail_aia_create;
    }
    ret = vart_riscv_aia_create(&aia, &vm,
                                KVM_DEV_RISCV_AIA_MODE_AUTO);
    if (ret < 0) {
        goto fail_aia_create;
    }
    if (aia.device.vm != &vm || aia.device.fd < 0 ||
        aia.device.type != KVM_DEV_TYPE_RISCV_AIA ||
        aia.mode > KVM_DEV_RISCV_AIA_MODE_AUTO ||
        vart_kvm_device_has_attr(&aia.device,
                                 KVM_DEV_RISCV_AIA_GRP_CONFIG,
                                 KVM_DEV_RISCV_AIA_CONFIG_IDS) < 0 ||
        vart_kvm_device_has_attr(&aia.device,
                                 KVM_DEV_RISCV_AIA_GRP_CTRL,
                                 KVM_DEV_RISCV_AIA_CTRL_INIT) < 0) {
        ret = -EIO;
        goto fail_aia;
    }

    vart_riscv_aia_destroy(&aia);
    vart_vm_destroy(&vm);
    vart_kvm_close(&kvm);
    printf("ok - create the in-kernel RISC-V AIA device\n");
    return EXIT_SUCCESS;

fail_aia:
    vart_riscv_aia_destroy(&aia);
fail_aia_create:
    vart_vm_destroy(&vm);
fail_vm:
    vart_kvm_close(&kvm);
    fprintf(stderr, "not ok - create RISC-V AIA device: %d\n", ret);
    return EXIT_FAILURE;
}
