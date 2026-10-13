#include <asm/kvm.h>
#include <errno.h>
#include <stdint.h>

#include "vart/pci.h"
#include "vart/riscv-aia.h"

static uint64_t msix_load_le(const uint8_t *data, unsigned int size)
{
    uint64_t value = 0;
    unsigned int i;

    for (i = 0; i < size; i++) {
        value |= (uint64_t)data[i] << (i * 8);
    }
    return value;
}

static int pci_msix_aia_inject(VartPciDevice *device, unsigned int vector,
                               void *opaque)
{
    VartRiscvAia *aia = opaque;
    const uint8_t *entry;
    uint64_t address;
    uint64_t offset;
    size_t vcpu_index;
    uint32_t interrupt_id;

    if (aia == NULL || !aia->initialized ||
        vector >= device->msix.vector_count) {
        return -EINVAL;
    }
    entry = &device->msix.table[vector * 16];
    address = msix_load_le(entry, 8);
    interrupt_id = (uint32_t)msix_load_le(entry + 8, 4);
    if (vart_pci_device_remap_msix_message(device, vector, &address,
                                           &interrupt_id) < 0) {
        return -EACCES;
    }
    if (address < aia->imsic_base) {
        return -EINVAL;
    }
    offset = address - aia->imsic_base;
    if (offset % KVM_DEV_RISCV_IMSIC_SIZE != 0) {
        return -EINVAL;
    }
    vcpu_index = offset / KVM_DEV_RISCV_IMSIC_SIZE;
    if (vcpu_index >= aia->vcpu_count) {
        return -EINVAL;
    }
    return vart_riscv_aia_signal_msi(aia, vcpu_index, interrupt_id);
}

int vart_pci_device_connect_msix_aia(VartPciDevice *device,
                                     VartRiscvAia *aia)
{
    if (aia == NULL || !aia->initialized) {
        return -EINVAL;
    }
    return vart_pci_device_set_msix_injector(device, pci_msix_aia_inject,
                                             aia);
}
