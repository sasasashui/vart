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
    aia->nr_sources = 0;
    aia->vcpu_count = 0;
    aia->imsic_base = 0;
    aia->aplic_base = 0;
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

static int riscv_aia_init(VartRiscvAia *aia, size_t vcpu_count,
                          uint64_t imsic_base, uint32_t nr_ids,
                          uint64_t aplic_base, uint32_t nr_sources)
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
        (nr_sources != 0 &&
         (nr_sources >= KVM_DEV_RISCV_AIA_SRCS_MAX ||
          aplic_base % KVM_DEV_RISCV_APLIC_ALIGN != 0)) ||
        vcpu_count - 1 >
            (UINT64_MAX - imsic_base) / KVM_DEV_RISCV_IMSIC_SIZE) {
        return -EINVAL;
    }
    hart_bits = hart_bits_for_count(vcpu_count);
    ret = 0;
    if (nr_sources != 0) {
        ret = vart_kvm_device_set_attr(&aia->device,
                                       KVM_DEV_RISCV_AIA_GRP_CONFIG,
                                       KVM_DEV_RISCV_AIA_CONFIG_SRCS,
                                       &nr_sources);
    }
    if (ret == 0 && nr_sources != 0) {
        ret = vart_kvm_device_set_attr(&aia->device,
                                       KVM_DEV_RISCV_AIA_GRP_ADDR,
                                       KVM_DEV_RISCV_AIA_ADDR_APLIC,
                                       &aplic_base);
    }
    if (ret == 0) {
        ret = vart_kvm_device_set_attr(&aia->device,
                                       KVM_DEV_RISCV_AIA_GRP_CONFIG,
                                       KVM_DEV_RISCV_AIA_CONFIG_IDS,
                                       &nr_ids);
    }
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
    aia->nr_sources = nr_sources;
    aia->vcpu_count = vcpu_count;
    aia->imsic_base = imsic_base;
    aia->aplic_base = aplic_base;
    aia->initialized = true;
    return 0;
}

int vart_riscv_aia_init_imsic(VartRiscvAia *aia, size_t vcpu_count,
                              uint64_t imsic_base, uint32_t nr_ids)
{
    return riscv_aia_init(aia, vcpu_count, imsic_base, nr_ids, 0, 0);
}

int vart_riscv_aia_init_aplic(VartRiscvAia *aia, size_t vcpu_count,
                              uint64_t imsic_base, uint32_t nr_ids,
                              uint64_t aplic_base, uint32_t nr_sources)
{
    if (nr_sources == 0) {
        return -EINVAL;
    }
    return riscv_aia_init(aia, vcpu_count, imsic_base, nr_ids,
                          aplic_base, nr_sources);
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

int vart_riscv_aia_set_irq(VartRiscvAia *aia, uint32_t irq, bool level)
{
    struct kvm_irq_level irq_level = {
        .irq = irq,
        .level = level,
    };

    if (aia == NULL || !aia->initialized || aia->nr_sources == 0 ||
        irq == 0 || irq > aia->nr_sources) {
        return -EINVAL;
    }
    if (ioctl(aia->device.vm->fd, KVM_IRQ_LINE, &irq_level) < 0) {
        return -errno;
    }
    return 0;
}

int vart_riscv_aia_pulse_irq(VartRiscvAia *aia, uint32_t irq)
{
    int ret;

    ret = vart_riscv_aia_set_irq(aia, irq, true);
    if (ret < 0) {
        return ret;
    }
    return vart_riscv_aia_set_irq(aia, irq, false);
}

static int riscv_aia_irq_set(void *opaque, uint32_t source, bool level)
{
    return vart_riscv_aia_set_irq(opaque, source, level);
}

int vart_riscv_aia_connect_irq(VartRiscvAia *aia, VartIrq *irq,
                               uint32_t source)
{
    if (aia == NULL || !aia->initialized || aia->nr_sources == 0 ||
        source == 0 || source > aia->nr_sources) {
        return -EINVAL;
    }
    return vart_irq_init(irq, riscv_aia_irq_set, aia, source);
}
