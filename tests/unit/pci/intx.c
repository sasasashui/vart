#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "vart/machine/virt.h"
#include "vart/pci.h"

typedef struct IntxSink {
    unsigned int changes;
    uint32_t line;
    bool level;
} IntxSink;

static int intx_set(void *opaque, uint32_t line, bool level)
{
    IntxSink *sink = opaque;

    sink->changes++;
    sink->line = line;
    sink->level = level;
    return 0;
}

static uint64_t ecam_address(uint8_t devfn, uint16_t offset)
{
    return VART_VIRT_PCIE_ECAM_BASE + ((uint64_t)devfn << 12) + offset;
}

static int check(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "not ok - %s\n", message);
        return EXIT_FAILURE;
    }
    return 0;
}

int main(void)
{
    VartAddressSpace address_space;
    VartBus system_bus;
    VartPciRootComplex complex;
    VartPciDevice device;
    VartIrq irq;
    IntxSink sink = { 0 };
    uint64_t value;

    vart_address_space_init(&address_space);
    vart_bus_init(&system_bus, "system-bus", NULL, &address_space);
    if (check(vart_irq_init(&irq, intx_set, &sink, 23) == 0,
              "INTx route initialization") != 0 ||
        check(vart_pci_root_complex_init(&complex, &system_bus,
                                         &address_space) == 0,
              "PCIe root complex initialization") != 0 ||
        check(vart_bus_add_device(&system_bus, &complex.parent) == 0,
              "PCIe root complex attachment") != 0 ||
        check(vart_device_realize(&complex.parent) == 0,
              "PCIe root complex realization") != 0 ||
        check(vart_pci_device_init(&device, "pci-intx-test", 0x1234, 0x5678,
                                   0, 0, 0, 2) == 0,
              "PCI function initialization") != 0 ||
        check(vart_pci_device_connect_intx(&device, 1, &irq) == 0,
              "INTx connection") != 0 ||
        check(vart_pci_bus_add_device(&complex.root_bus, &device, 8) == 0,
              "PCI function attachment") != 0 ||
        check(vart_device_realize(&device.parent) == 0,
              "PCI function realization") != 0) {
        return EXIT_FAILURE;
    }
    if (check(vart_address_space_read(&address_space,
                                     ecam_address(8, 0x3c), 2, &value) == 0 &&
                  value == UINT16_C(0x0117),
              "INTx configuration registers") != 0 ||
        check(vart_pci_device_set_intx(&device, true) == 0 && sink.changes == 1 &&
                  sink.line == 23 && sink.level,
              "INTx assertion") != 0 ||
        check(vart_pci_device_set_intx(&device, true) == 0 && sink.changes == 1,
              "INTx level coalescing") != 0 ||
        check(vart_pci_device_set_intx(&device, false) == 0 && sink.changes == 2 &&
                  !sink.level,
              "INTx deassertion") != 0 ||
        check(vart_pci_device_set_intx(&device, true) == 0,
              "INTx reassertion") != 0 ||
        check(vart_pci_device_reset(&device) == 0 && !sink.level &&
                  !device.intx_asserted,
              "INTx reset deassertion") != 0) {
        return EXIT_FAILURE;
    }
    vart_device_unrealize(&device.parent);
    if (check(vart_pci_bus_remove_device(&complex.root_bus, &device) == 0,
              "PCI function removal") != 0 ||
        check(vart_pci_root_complex_destroy(&complex) == 0,
              "PCIe root complex destruction") != 0 ||
        check(vart_bus_destroy(&system_bus) == 0,
              "system bus destruction") != 0) {
        return EXIT_FAILURE;
    }
    vart_address_space_destroy(&address_space);
    puts("ok - PCI INTx level interrupt path");
    return EXIT_SUCCESS;
}
