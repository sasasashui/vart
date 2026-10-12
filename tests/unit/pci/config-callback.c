#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "vart/machine/virt.h"
#include "vart/pci.h"

typedef struct ConfigState {
    uint32_t value;
    unsigned int reads;
    unsigned int writes;
} ConfigState;

typedef struct ConfigDevice {
    VartPciDevice pci;
    ConfigState state;
} ConfigDevice;

static int config_read(VartPciDevice *device, unsigned int offset,
                       unsigned int size, uint64_t *value)
{
    ConfigDevice *config_device = (ConfigDevice *)device;

    if (offset != 0x40 || size != 4) {
        return -ENOENT;
    }
    config_device->state.reads++;
    *value = config_device->state.value;
    return 0;
}

static int config_write(VartPciDevice *device, unsigned int offset,
                        unsigned int size, uint64_t value)
{
    ConfigDevice *config_device = (ConfigDevice *)device;

    if (offset != 0x40 || size != 4) {
        return -ENOENT;
    }
    config_device->state.writes++;
    config_device->state.value = value;
    return 0;
}

static const VartPciConfigOps config_ops = {
    .read = config_read,
    .write = config_write,
};

static uint64_t ecam_address(uint8_t devfn, uint16_t offset)
{
    return VART_VIRT_PCIE_ECAM_BASE + ((uint64_t)devfn << 12) + offset;
}

int main(void)
{
    VartAddressSpace address_space;
    VartBus system_bus;
    VartPciRootComplex complex;
    ConfigDevice config_device = {
        .state = { .value = UINT32_C(0x11223344) },
    };
    uint64_t value;
    const uint8_t devfn = 8;

    vart_address_space_init(&address_space);
    vart_bus_init(&system_bus, "system-bus", NULL, &address_space);
    if (vart_pci_root_complex_init(&complex, &system_bus, &address_space) < 0 ||
        vart_bus_add_device(&system_bus, &complex.parent) < 0 ||
        vart_device_realize(&complex.parent) < 0 ||
        vart_pci_device_init(&config_device.pci, "pci-config-test",
                             UINT16_C(0x1234), UINT16_C(0x5678), 1, 0, 0, 2) <
            0 ||
        vart_pci_device_set_config_ops(&config_device.pci, &config_ops) < 0 ||
        vart_pci_bus_add_device(&complex.root_bus, &config_device.pci, devfn) <
            0 ||
        vart_device_realize(&config_device.pci.parent) < 0 ||
        vart_address_space_read(&address_space,
                                ecam_address(devfn, 0x40), 4, &value) < 0 ||
        value != UINT32_C(0x11223344) || config_device.state.reads != 1 ||
        vart_address_space_write(&address_space, ecam_address(devfn, 0x40),
                                 4, UINT32_C(0xaabbccdd)) < 0 ||
        config_device.state.value != UINT32_C(0xaabbccdd) ||
        config_device.state.writes != 1 ||
        vart_address_space_read(&address_space,
                                ecam_address(devfn, 0x00), 4, &value) < 0 ||
        value != UINT32_C(0x56781234) || config_device.state.reads != 1 ||
        vart_address_space_write(&address_space, ecam_address(devfn, 0x04),
                                 2, 7) < 0 || config_device.state.writes != 1) {
        fprintf(stderr, "not ok - PCI config callback dispatch\n");
        return EXIT_FAILURE;
    }

    vart_device_unrealize(&config_device.pci.parent);
    if (vart_pci_bus_remove_device(&complex.root_bus, &config_device.pci) < 0 ||
        vart_pci_root_complex_destroy(&complex) < 0 ||
        vart_bus_destroy(&system_bus) < 0) {
        fprintf(stderr, "not ok - PCI config callback teardown\n");
        return EXIT_FAILURE;
    }
    vart_address_space_destroy(&address_space);
    puts("ok - PCI config callback dispatch");
    return EXIT_SUCCESS;
}
