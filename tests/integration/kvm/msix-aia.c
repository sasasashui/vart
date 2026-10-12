#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "vart/machine/virt-machine.h"
#include "vart/machine/virt.h"

static int bar_read(void *opaque, uint64_t offset, unsigned int size,
                    uint64_t *value)
{
    (void)opaque;
    (void)offset;
    if (size != 4 && size != 8) {
        return -EINVAL;
    }
    *value = 0;
    return 0;
}

static int bar_write(void *opaque, uint64_t offset, unsigned int size,
                     uint64_t value)
{
    (void)opaque;
    (void)offset;
    (void)value;
    return size == 4 || size == 8 ? 0 : -EINVAL;
}

static const VartMmioOps bar_ops = {
    .read = bar_read,
    .write = bar_write,
};

static uint64_t ecam_address(uint8_t devfn, uint16_t offset)
{
    return VART_VIRT_PCIE_ECAM_BASE + ((uint64_t)devfn << 12) + offset;
}

int main(void)
{
    VartVirtMachineConfig config = {
        .ram_size = 64 * 1024 * 1024,
        .vcpu_count = 1,
    };
    VartVirtMachine machine;
    VartPciDevice device;
    VartKvm kvm;
    uint64_t table_address;
    uint8_t devfn = 8;
    int ret;

    ret = vart_kvm_open(&kvm);
    if (ret < 0) {
        fprintf(stderr, "not ok - open KVM: %d\n", ret);
        return EXIT_FAILURE;
    }
    ret = vart_virt_machine_create(&machine, &kvm, &config);
    if (ret < 0) {
        vart_kvm_close(&kvm);
        fprintf(stderr, "not ok - create AIA machine: %d\n", ret);
        return EXIT_FAILURE;
    }
    ret = vart_pci_device_init(&device, "pci-msix-aia-test",
                               UINT16_C(0x1234), UINT16_C(0x5678), 1, 0, 0, 2);
    if (ret == 0) {
        ret = vart_pci_device_set_bar(&device, 0, UINT64_C(0x2000), 0,
                                      &bar_ops, NULL);
    }
    if (ret == 0) {
        ret = vart_pci_device_init_msix(&device, 4, 0, 0x100, 0, 0x1000);
    }
    if (ret == 0) {
        ret = vart_pci_bus_add_device(&machine.pcie.root_bus, &device, devfn);
    }
    if (ret == 0) {
        ret = vart_device_realize(&device.parent);
    }
    if (ret == 0) {
        ret = vart_pci_device_connect_msix_aia(&device, &machine.aia);
    }
    table_address = device.bars[0].address + 0x100;
    if (ret == 0) {
        ret = vart_address_space_write(&vart_system.system_address_space,
                                       table_address, 8,
                                       machine.aia.imsic_base);
    }
    if (ret == 0) {
        ret = vart_address_space_write(&vart_system.system_address_space,
                                       table_address + 8, 4, 9);
    }
    if (ret == 0) {
        ret = vart_address_space_write(&vart_system.system_address_space,
                                       ecam_address(devfn,
                                                    device.msix.capability_offset +
                                                        2),
                                       2, UINT16_C(0x8003));
    }
    if (ret == 0) {
        ret = vart_pci_device_raise_msix(&device, 0);
    }
    vart_device_unrealize(&device.parent);
    if (device.parent.bus != NULL) {
        vart_pci_bus_remove_device(&machine.pcie.root_bus, &device);
    }
    vart_virt_machine_destroy(&machine);
    vart_kvm_close(&kvm);
    if (ret < 0) {
        fprintf(stderr, "not ok - inject MSI-X through KVM AIA: %d\n", ret);
        return EXIT_FAILURE;
    }
    puts("ok - inject MSI-X through KVM AIA");
    return EXIT_SUCCESS;
}
