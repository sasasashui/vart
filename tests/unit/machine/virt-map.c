#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "vart/machine/virt.h"

int main(void)
{
    if (VART_VIRT_UART_BASE != UINT64_C(0x10000000) ||
        VART_VIRT_UART_SIZE != 0x100 || VART_VIRT_UART_IRQ != 10 ||
        VART_VIRT_DRAM_BASE != UINT64_C(0x80000000) ||
        VART_VIRT_APLIC_M_BASE != UINT64_C(0x0c000000) ||
        VART_VIRT_APLIC_S_BASE != UINT64_C(0x0d000000) ||
        VART_VIRT_IMSIC_M_BASE + VART_VIRT_IMSIC_MAX_SIZE >
            VART_VIRT_IMSIC_S_BASE ||
        VART_VIRT_IMSIC_S_BASE + VART_VIRT_IMSIC_MAX_SIZE >
            VART_VIRT_PCIE_ECAM_BASE ||
        VART_VIRT_PCIE_MMIO_BASE + VART_VIRT_PCIE_MMIO_SIZE !=
            VART_VIRT_DRAM_BASE ||
        vart_virt_imsic_hart_size(0) != 0x1000 ||
        vart_virt_imsic_hart_size(3) != 0x8000 ||
        vart_virt_imsic_hart_base(VART_VIRT_IMSIC_S_BASE, 1, 2, 3) !=
            UINT64_C(0x29010000)) {
        fprintf(stderr, "not ok - QEMU virt machine map\n");
        return EXIT_FAILURE;
    }
    puts("ok - validate QEMU-compatible RISC-V virt machine map");
    return EXIT_SUCCESS;
}
