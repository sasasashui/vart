#include <errno.h>
#include <string.h>

#include "vart/devices/uart16550.h"

#define UART_LCR_DLAB 0x80
#define UART_IIR_NO_INT 0x01
#define UART_IIR_RDI 0x04
#define UART_IIR_THRI 0x02
#define UART_IER_RDI 0x01
#define UART_IER_THRI 0x02

static int uart_update_irq(VartUart16550 *uart)
{
    bool receive = (uart->ier & UART_IER_RDI) &&
                   (uart->lsr & VART_UART16550_LSR_DR);
    bool transmit = uart->ier & UART_IER_THRI;
    bool pending = receive || transmit;
    int ret;

    ret = uart->irq == NULL ? 0 : vart_irq_set(uart->irq, pending);
    if (ret == 0) {
        uart->iir = receive ? UART_IIR_RDI :
                    transmit ? UART_IIR_THRI : UART_IIR_NO_INT;
    }
    return ret;
}

static int uart_read(void *opaque, uint64_t offset, unsigned int size,
                     uint64_t *value)
{
    VartUart16550 *uart = opaque;

    if (size != 1 || offset >= 8) {
        return -EINVAL;
    }
    switch (offset) {
    case 0:
        if (uart->lcr & UART_LCR_DLAB) {
            *value = uart->divisor & 0xff;
        } else {
            int ret;

            *value = uart->rbr;
            uart->lsr &= ~VART_UART16550_LSR_DR;
            ret = uart_update_irq(uart);
            if (ret < 0) {
                uart->lsr |= VART_UART16550_LSR_DR;
            }
            return ret;
        }
        break;
    case 1:
        *value = uart->lcr & UART_LCR_DLAB ? uart->divisor >> 8 : uart->ier;
        break;
    case 2: *value = uart->iir; break;
    case 3: *value = uart->lcr; break;
    case 4: *value = uart->mcr; break;
    case 5: *value = uart->lsr; break;
    case 6: *value = uart->msr; break;
    case 7: *value = uart->scr; break;
    default: return -EINVAL;
    }
    return 0;
}

static int uart_write(void *opaque, uint64_t offset, unsigned int size,
                      uint64_t value)
{
    VartUart16550 *uart = opaque;

    if (size != 1 || offset >= 8) {
        return -EINVAL;
    }
    switch (offset) {
    case 0:
        if (uart->lcr & UART_LCR_DLAB) {
            uart->divisor = (uart->divisor & 0xff00) | value;
        } else if (uart->output != NULL) {
            uart->output(uart->output_opaque, value);
        }
        break;
    case 1:
        if (uart->lcr & UART_LCR_DLAB) {
            uart->divisor = (uart->divisor & 0x00ff) | (value << 8);
        } else {
            uint8_t old_ier = uart->ier;
            int ret;

            uart->ier = value & 0x0f;
            ret = uart_update_irq(uart);
            if (ret < 0) {
                uart->ier = old_ier;
            }
            return ret;
        }
        break;
    case 2: uart->fcr = value & 0xc9; break;
    case 3: uart->lcr = value; break;
    case 4: uart->mcr = value & 0x1f; break;
    case 5: case 6: break;
    case 7: uart->scr = value; break;
    default: return -EINVAL;
    }
    return 0;
}

static const VartMmioOps uart_ops = { .read = uart_read, .write = uart_write };

int vart_uart16550_init(VartUart16550 *uart, uint64_t base,
                        VartUartOutput output, void *output_opaque)
{
    memset(uart, 0, sizeof(*uart));
    uart->output = output;
    uart->output_opaque = output_opaque;
    vart_uart16550_reset(uart);
    return vart_address_region_init_mmio(&uart->region, base,
                                         VART_UART16550_MMIO_SIZE, 0, uart,
                                         &uart_ops, uart);
}

void vart_uart16550_reset(VartUart16550 *uart)
{
    if (uart->irq != NULL) {
        vart_irq_lower(uart->irq);
    }
    uart->ier = 0;
    uart->iir = UART_IIR_NO_INT;
    uart->fcr = 0;
    uart->lcr = 0;
    uart->mcr = 0;
    uart->lsr = VART_UART16550_LSR_THRE | VART_UART16550_LSR_TEMT;
    uart->msr = 0;
    uart->scr = 0;
    uart->rbr = 0;
    uart->divisor = 0;
}

int vart_uart16550_connect_irq(VartUart16550 *uart, VartIrq *irq)
{
    int ret;

    if (uart == NULL || irq == NULL) {
        return -EINVAL;
    }
    uart->irq = irq;
    ret = uart_update_irq(uart);
    if (ret < 0) {
        uart->irq = NULL;
    }
    return ret;
}

int vart_uart16550_receive(VartUart16550 *uart, unsigned char value)
{
    uint8_t old_rbr;
    int ret;

    if (uart == NULL) {
        return -EINVAL;
    }
    if (uart->lsr & VART_UART16550_LSR_DR) {
        return -EAGAIN;
    }
    old_rbr = uart->rbr;
    uart->rbr = value;
    uart->lsr |= VART_UART16550_LSR_DR;
    ret = uart_update_irq(uart);
    if (ret < 0) {
        uart->lsr &= ~VART_UART16550_LSR_DR;
        uart->rbr = old_rbr;
    }
    return ret;
}
