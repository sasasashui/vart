#include <asm/kvm.h>
#include <errno.h>
#include <linux/kvm.h>
#include <string.h>

#include "vart/riscv-aia.h"

int vart_riscv_aia_create(VartRiscvAia *aia, VartVm *vm, uint32_t mode)
{
    int ret;

    if (aia == NULL || vm == NULL || mode > KVM_DEV_RISCV_AIA_MODE_AUTO) {
        return -EINVAL;
    }
    memset(aia, 0, sizeof(*aia));
    aia->device.fd = -1;
    ret = vart_kvm_device_create(&aia->device, vm,
                                 KVM_DEV_TYPE_RISCV_AIA);
    if (ret < 0) {
        return ret;
    }
    ret = vart_kvm_device_has_attr(&aia->device,
                                   KVM_DEV_RISCV_AIA_GRP_CONFIG,
                                   KVM_DEV_RISCV_AIA_CONFIG_MODE);
    if (ret < 0) {
        goto fail;
    }
    ret = vart_kvm_device_set_attr(&aia->device,
                                   KVM_DEV_RISCV_AIA_GRP_CONFIG,
                                   KVM_DEV_RISCV_AIA_CONFIG_MODE, &mode);
    if (ret < 0) {
        goto fail;
    }
    ret = vart_kvm_device_get_attr(&aia->device,
                                   KVM_DEV_RISCV_AIA_GRP_CONFIG,
                                   KVM_DEV_RISCV_AIA_CONFIG_MODE,
                                   &aia->mode);
    if (ret < 0) {
        goto fail;
    }
    return 0;

fail:
    vart_riscv_aia_destroy(aia);
    return ret;
}

void vart_riscv_aia_destroy(VartRiscvAia *aia)
{
    if (aia == NULL) {
        return;
    }
    vart_kvm_device_destroy(&aia->device);
    aia->mode = 0;
}
