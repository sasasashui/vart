#ifndef VART_DEVICES_UART16550_H
#define VART_DEVICES_UART16550_H

#include <stdint.h>

#include "vart/address-space.h"

#define VART_UART16550_MMIO_SIZE UINT64_C(0x100)
#define VART_UART16550_LSR_THRE 0x20
#define VART_UART16550_LSR_TEMT 0x40

typedef void (*VartUartOutput)(void *opaque, unsigned char value);

typedef struct VartUart16550 {
    VartAddressRegion region;
    uint8_t ier;
    uint8_t iir;
    uint8_t fcr;
    uint8_t lcr;
    uint8_t mcr;
    uint8_t lsr;
    uint8_t msr;
    uint8_t scr;
    uint16_t divisor;
    VartUartOutput output;
    void *output_opaque;
} VartUart16550;

int vart_uart16550_init(VartUart16550 *uart, uint64_t base,
                        VartUartOutput output, void *output_opaque);
void vart_uart16550_reset(VartUart16550 *uart);

#endif
