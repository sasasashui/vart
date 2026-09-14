#include <asm/kvm.h>
#include <errno.h>
#include <linux/kvm.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>

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
    aia->nr_ids = 0;
    aia->vcpu_count = 0;
    aia->imsic_base = 0;
    aia->initialized = false;
}

static uint32_t hart_bits_for_count(size_t count)
{
    uint32_t bits = 0;
    size_t max_index = count - 1;

    while (max_index != 0) {
        bits++;
        max_index >>= 1;
    }
    return bits;
}

int vart_riscv_aia_init_imsic(VartRiscvAia *aia, size_t vcpu_count,
                              uint64_t imsic_base, uint32_t nr_ids)
{
    uint32_t guest_bits = 0;
    uint32_t hart_bits;
    size_t i;
    int ret;

    if (aia == NULL || aia->device.fd < 0 || aia->initialized ||
        vcpu_count == 0 || vcpu_count > KVM_DEV_RISCV_APLIC_MAX_HARTS ||
        nr_ids < KVM_DEV_RISCV_AIA_IDS_MIN ||
        nr_ids >= KVM_DEV_RISCV_AIA_IDS_MAX ||
        (nr_ids & KVM_DEV_RISCV_AIA_IDS_MIN) !=
            KVM_DEV_RISCV_AIA_IDS_MIN ||
        imsic_base % KVM_DEV_RISCV_IMSIC_ALIGN != 0 ||
        vcpu_count - 1 >
            (UINT64_MAX - imsic_base) / KVM_DEV_RISCV_IMSIC_SIZE) {
        return -EINVAL;
    }
    hart_bits = hart_bits_for_count(vcpu_count);
    ret = vart_kvm_device_set_attr(&aia->device,
                                   KVM_DEV_RISCV_AIA_GRP_CONFIG,
                                   KVM_DEV_RISCV_AIA_CONFIG_IDS, &nr_ids);
    if (ret == 0) {
        ret = vart_kvm_device_set_attr(&aia->device,
                                       KVM_DEV_RISCV_AIA_GRP_CONFIG,
                                       KVM_DEV_RISCV_AIA_CONFIG_GUEST_BITS,
                                       &guest_bits);
    }
    for (i = 0; ret == 0 && i < vcpu_count; i++) {
        uint64_t address = imsic_base + i * KVM_DEV_RISCV_IMSIC_SIZE;

        ret = vart_kvm_device_set_attr(
            &aia->device, KVM_DEV_RISCV_AIA_GRP_ADDR,
            KVM_DEV_RISCV_AIA_ADDR_IMSIC(i), &address);
    }
    if (ret == 0) {
        ret = vart_kvm_device_set_attr(&aia->device,
                                       KVM_DEV_RISCV_AIA_GRP_CONFIG,
                                       KVM_DEV_RISCV_AIA_CONFIG_HART_BITS,
                                       &hart_bits);
    }
    if (ret == 0) {
        ret = vart_kvm_device_set_attr(&aia->device,
                                       KVM_DEV_RISCV_AIA_GRP_CTRL,
                                       KVM_DEV_RISCV_AIA_CTRL_INIT, NULL);
    }
    if (ret < 0) {
        return ret;
    }
    aia->nr_ids = nr_ids;
    aia->vcpu_count = vcpu_count;
    aia->imsic_base = imsic_base;
    aia->initialized = true;
    return 0;
}

int vart_riscv_aia_signal_msi(VartRiscvAia *aia, size_t vcpu_index,
                              uint32_t interrupt_id)
{
    struct kvm_msi msi = { 0 };
    uint64_t address;

    if (aia == NULL || !aia->initialized ||
        vcpu_index >= aia->vcpu_count || interrupt_id == 0 ||
        interrupt_id > aia->nr_ids) {
        return -EINVAL;
    }
    address = aia->imsic_base +
              vcpu_index * KVM_DEV_RISCV_IMSIC_SIZE;
    msi.address_lo = address;
    msi.address_hi = address >> 32;
    msi.data = interrupt_id;
    if (ioctl(aia->device.vm->fd, KVM_SIGNAL_MSI, &msi) < 0) {
        return -errno;
    }
    return 0;
}
