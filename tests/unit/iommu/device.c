#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vart/bus.h"
#include "vart/iommu.h"
#include "vart/machine/virt.h"
#include "vart/pci.h"
#include "vart/riscv-iommu.h"

static uint64_t ecam_address(uint8_t bus, uint8_t devfn, uint16_t offset)
{
    return VART_VIRT_PCIE_ECAM_BASE + ((uint64_t)bus << 20) +
           ((uint64_t)devfn << 12) + offset;
}

static int check(bool condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "not ok - %s\n", message);
        return -1;
    }
    return 0;
}

static int test_msix_inject(VartPciDevice *device, unsigned int vector,
                            void *opaque)
{
    unsigned int *count = opaque;

    (void)device;
    if (vector != 0) {
        return -EINVAL;
    }
    (*count)++;
    return 0;
}

static void store_le64(uint8_t *data, uint64_t value)
{
    for (unsigned int i = 0; i < sizeof(value); i++) {
        data[i] = value >> (i * 8);
    }
}

int main(void)
{
    uint8_t backing[0x10000] = { 0 };
    VartAddressSpace address_space;
    VartAddressRegion ram;
    VartBus system_bus;
    VartPciRootComplex complex;
    VartRiscvIommu iommu;
    VartIommuContext context;
    VartPciDevice client;
    uint64_t config_bar;
    uint64_t bar_address;
    uint64_t queue_address = VART_VIRT_DRAM_BASE + UINT64_C(0x2000);
    uint64_t ddt_address = VART_VIRT_DRAM_BASE + UINT64_C(0x3000);
    uint64_t dc_address = VART_VIRT_DRAM_BASE + UINT64_C(0x4000);
    uint64_t ddt_l2_address = VART_VIRT_DRAM_BASE + UINT64_C(0x5000);
    uint64_t generation;
    uint64_t fault_address;
    uint64_t fault_info;
    uint64_t capability_offset;
    uint8_t fault_data[4];
    uint8_t client_data[] = "DDT DMA";
    unsigned int msix_count = 0;
    int ret;

    vart_address_space_init(&address_space);
    vart_bus_init(&system_bus, "system-bus", NULL, &address_space);
    ret = vart_address_region_init_ram(&ram, VART_VIRT_DRAM_BASE,
                                       sizeof(backing), -1, NULL, backing);
    if (ret < 0) {
        fprintf(stderr, "setup: RAM init: %d\n", ret);
        return 1;
    }
    ret = vart_address_space_add(&address_space, &ram);
    if (ret < 0) {
        fprintf(stderr, "setup: RAM add: %d\n", ret);
        return 1;
    }
    ret = vart_pci_root_complex_init(&complex, &system_bus, &address_space);
    if (ret < 0) {
        fprintf(stderr, "setup: PCI root: %d\n", ret);
        return 1;
    }
    ret = vart_bus_add_device(&system_bus, &complex.parent);
    if (ret < 0) {
        fprintf(stderr, "setup: PCI root bus: %d\n", ret);
        return 1;
    }
    ret = vart_device_realize(&complex.parent);
    if (ret < 0) {
        fprintf(stderr, "setup: PCI root realize: %d\n", ret);
        return 1;
    }
    ret = vart_iommu_context_init(&context, &address_space,
                                  VART_IOMMU_CONTEXT_IDENTITY);
    if (ret < 0) {
        fprintf(stderr, "setup: context: %d\n", ret);
        return 1;
    }
    ret = vart_riscv_iommu_init(&iommu, &address_space);
    if (ret < 0) {
        fprintf(stderr, "setup: IOMMU init: %d\n", ret);
        return 1;
    }
    ret = vart_riscv_iommu_set_context(&iommu, &context);
    if (ret < 0) {
        fprintf(stderr, "setup: IOMMU context: %d\n", ret);
        return 1;
    }
    ret = vart_pci_device_init(&client, "iommu-dma-client", 0x1234, 0x5678,
                               0, 0, 0, 0xff);
    if (ret < 0 || vart_riscv_iommu_bind_device(&iommu, &client, &context) < 0 ||
        vart_pci_bus_add_device(&complex.root_bus, &client, 8) < 0 ||
        vart_device_realize(&client.parent) < 0) {
        fprintf(stderr, "setup: DDT client\n");
        return 1;
    }
    ret = vart_riscv_iommu_set_msix_injector(&iommu, test_msix_inject,
                                             &msix_count);
    if (ret < 0) {
        fprintf(stderr, "setup: IOMMU MSI-X injector: %d\n", ret);
        return 1;
    }
    ret = vart_pci_bus_add_device(&complex.root_bus, &iommu.pci, 0);
    if (ret < 0) {
        fprintf(stderr, "setup: IOMMU bus: %d\n", ret);
        return 1;
    }
    ret = vart_device_realize(&iommu.pci.parent);
    if (ret < 0) {
        fprintf(stderr, "setup: IOMMU realize: %d\n", ret);
        return 1;
    }

    if (check(vart_address_space_read(&address_space,
                                     ecam_address(0, 0, 0), 4,
                                     &config_bar) == 0 &&
                  (config_bar & UINT32_MAX) ==
                      ((uint32_t)VART_RISCV_IOMMU_DEVICE_ID << 16 |
                       VART_RISCV_IOMMU_VENDOR_ID),
              "IOMMU PCI identity") < 0 ||
        check(vart_address_space_read(&address_space,
                                      ecam_address(0, 0, 0x10), 4,
                                      &config_bar) == 0,
              "read IOMMU BAR") < 0) {
        return 1;
    }
    bar_address = config_bar & UINT32_C(0xfffffff0);
    ret = vart_address_space_write(&address_space, ecam_address(0, 0, 0x04),
                                   2, VART_PCI_COMMAND_MEMORY_ENABLE |
                                      VART_PCI_COMMAND_BUS_MASTER);
    if (check(ret == 0, "enable IOMMU PCI memory and bus mastering") < 0) {
        return 1;
    }
    ret = vart_address_space_write(&address_space,
                                   ecam_address(0, 8, 0x04), 2,
                                   VART_PCI_COMMAND_BUS_MASTER);
    if (check(ret == 0, "enable DDT client bus mastering") < 0) {
        return 1;
    }
    ret = vart_address_space_read(&address_space, ecam_address(0, 0, 0x34),
                                  1, &capability_offset);
    ret |= vart_address_space_write(&address_space,
                                    ecam_address(0, 0, capability_offset + 2),
                                    2, UINT16_C(0x8000));
    if (check(ret == 0, "enable IOMMU MSI-X") < 0) {
        return 1;
    }
    if (check(vart_address_space_read(&address_space, bar_address +
                                      VART_RISCV_IOMMU_REG_CAP, 8,
                                      &config_bar) == 0 &&
                  (config_bar & VART_RISCV_IOMMU_CAP_SV39X4) != 0,
              "read standard IOMMU capabilities") < 0) {
        return 1;
    }

    /* In mode 2, the DDT contains device contexts directly. */
    store_le64(backing + (ddt_address - VART_VIRT_DRAM_BASE) + 8 * 64,
               VART_RISCV_IOMMU_DC_TC_VALID);
    store_le64(backing + (ddt_address - VART_VIRT_DRAM_BASE) + 8 * 64 + 24,
               0);
    ret = vart_address_space_write(&address_space,
                                   bar_address + VART_RISCV_IOMMU_REG_DDTP, 8,
                                   (ddt_address >> 12 << 10) | 2);
    ret |= vart_pci_device_dma_write(&client, VART_VIRT_DRAM_BASE + 0x5000,
                                     client_data, sizeof(client_data));
    if (check(ret == 0 &&
                  memcmp(backing + 0x5000, client_data,
                         sizeof(client_data)) == 0,
              "translate a bound requester through a one-level DDT") < 0) {
        return 1;
    }
    store_le64(backing + (ddt_address - VART_VIRT_DRAM_BASE) + 8 * 64, 0);
    ret = vart_address_space_write(&address_space,
                                   bar_address + VART_RISCV_IOMMU_REG_DDTP, 8,
                                   (ddt_address >> 12 << 10) | 2);
    if (check(ret == -EINVAL, "reject an invalid DDT entry") < 0) {
        return 1;
    }
    store_le64(backing + (ddt_address - VART_VIRT_DRAM_BASE) + 8 * 64,
               VART_RISCV_IOMMU_DC_TC_VALID);
    if (check(vart_address_space_write(
                  &address_space, bar_address + VART_RISCV_IOMMU_REG_DDTP, 8,
                  (ddt_address >> 12 << 10) | 2) == 0,
              "reload a valid DDT entry") < 0) {
        return 1;
    }

    /* A two-level DDT uses a directory DTE and a leaf device context. */
    store_le64(backing + (ddt_address - VART_VIRT_DRAM_BASE),
               (dc_address >> 12 << 10) | VART_RISCV_IOMMU_DDTE_VALID);
    store_le64(backing + (dc_address - VART_VIRT_DRAM_BASE) + 8 * 64,
               VART_RISCV_IOMMU_DC_TC_VALID);
    store_le64(backing + (dc_address - VART_VIRT_DRAM_BASE) + 8 * 64 + 24,
               0);
    ret = vart_address_space_write(&address_space,
                                   bar_address + VART_RISCV_IOMMU_REG_DDTP, 8,
                                   (ddt_address >> 12 << 10) | 3);
    ret |= vart_pci_device_dma_write(&client, VART_VIRT_DRAM_BASE + 0x6000,
                                     client_data, sizeof(client_data));
    if (check(ret == 0 &&
                  memcmp(backing + 0x6000, client_data,
                         sizeof(client_data)) == 0,
              "translate a bound requester through a two-level DDT") < 0) {
        return 1;
    }

    /* A three-level DDT adds one more directory page before the leaf. */
    store_le64(backing + (ddt_address - VART_VIRT_DRAM_BASE),
               (ddt_l2_address >> 12 << 10) | VART_RISCV_IOMMU_DDTE_VALID);
    store_le64(backing + (ddt_l2_address - VART_VIRT_DRAM_BASE),
               (dc_address >> 12 << 10) | VART_RISCV_IOMMU_DDTE_VALID);
    ret = vart_address_space_write(&address_space,
                                   bar_address + VART_RISCV_IOMMU_REG_DDTP, 8,
                                   (ddt_address >> 12 << 10) | 4);
    ret |= vart_pci_device_dma_write(&client, VART_VIRT_DRAM_BASE + 0x7000,
                                     client_data, sizeof(client_data));
    if (check(ret == 0 &&
                  memcmp(backing + 0x7000, client_data,
                         sizeof(client_data)) == 0,
              "translate a bound requester through a three-level DDT") < 0) {
        return 1;
    }

    store_le64(backing + (queue_address - VART_VIRT_DRAM_BASE),
               VART_RISCV_IOMMU_CMD_IOTINVAL | VART_RISCV_IOMMU_CMD_AV);
    store_le64(backing + (queue_address - VART_VIRT_DRAM_BASE) + 8,
               UINT64_C(0x00403000) >> 2);
    ret = vart_address_space_write(&address_space,
                                   bar_address + VART_RISCV_IOMMU_REG_CQB, 8,
                                   (queue_address >> 12 << 10) | 1);
    ret |= vart_address_space_write(&address_space,
                                    bar_address + VART_RISCV_IOMMU_REG_CQCSR,
                                    4, VART_RISCV_IOMMU_CQCSR_CQEN);
    ret |= vart_address_space_write(&address_space,
                                    bar_address + VART_RISCV_IOMMU_REG_CQT, 4,
                                    1);
    generation = context.invalidation_generation;
    if (check(ret == 0 && generation == 1,
              "process an IOTINVAL command from guest memory") < 0 ||
        check(vart_address_space_read(&address_space,
                                      bar_address + VART_RISCV_IOMMU_REG_CQH,
                                      4, &config_bar) == 0 && config_bar == 1,
              "advance command queue head") < 0) {
        return 1;
    }

    ret = vart_iommu_context_set_mode(&context, VART_IOMMU_CONTEXT_FAULT);
    ret |= vart_address_space_read_buffer(
        vart_iommu_context_address_space(&context), VART_VIRT_DRAM_BASE,
        fault_data, sizeof(fault_data));
    if (check(ret == -EACCES, "report a translation fault") < 0 ||
        check(vart_address_space_read(&address_space,
                                      bar_address +
                                          VART_RISCV_IOMMU_REG_FAULT_ADDR,
                                      8, &fault_address) == 0 &&
                  fault_address == VART_VIRT_DRAM_BASE,
              "record fault address") < 0 ||
        check(vart_address_space_read(&address_space,
                                      bar_address +
                                          VART_RISCV_IOMMU_REG_FAULT_INFO,
                                      8, &fault_info) == 0 &&
                  (fault_info & VART_RISCV_IOMMU_FAULT_INFO_WRITE) == 0,
              "record fault access direction") < 0 ||
        check(msix_count == 1, "notify fault through MSI-X") < 0) {
        return 1;
    }
    ret = vart_address_space_write(&address_space,
                                   bar_address +
                                       VART_RISCV_IOMMU_REG_FAULT_STATUS,
                                   4,
                                   VART_RISCV_IOMMU_FAULT_STATUS_PENDING);
    if (check(ret == 0, "clear fault pending status") < 0) {
        return 1;
    }

    if (check(vart_riscv_iommu_reset(&iommu) == 0,
              "reset IOMMU registers") < 0 ||
        check(vart_address_space_read(&address_space,
                                      bar_address + VART_RISCV_IOMMU_REG_CQH,
                                      4, &config_bar) == 0 && config_bar == 0,
              "reset command queue state") < 0) {
        return 1;
    }
    vart_device_unrealize(&iommu.pci.parent);
    vart_device_unrealize(&client.parent);
    if (check(vart_riscv_iommu_unbind_device(&iommu, &client) == 0,
              "unbind DDT client") < 0 ||
        check(vart_pci_bus_remove_device(&complex.root_bus, &client) == 0,
              "remove DDT client") < 0 ||
        check(vart_pci_bus_remove_device(&complex.root_bus, &iommu.pci) == 0,
              "remove IOMMU PCI device") < 0 ||
        check(vart_riscv_iommu_destroy(&iommu) == 0,
              "destroy IOMMU device") < 0) {
        return 1;
    }
    vart_iommu_context_destroy(&context);
    vart_pci_root_complex_destroy(&complex);
    vart_address_space_remove(&address_space, &ram);
    vart_bus_destroy(&system_bus);
    vart_address_space_destroy(&address_space);
    puts("ok - RISC-V IOMMU PCI registers and command queue");
    return 0;
}
