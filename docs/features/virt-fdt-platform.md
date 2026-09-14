# Virt platform device tree

## Purpose

The machine DTB exposes a QEMU-compatible simple platform bus and selects the
existing polling NS16550A model as the Linux boot console. Device discovery is
kept separate from the UART register implementation.

## Guest-visible layout

`/soc` is an identity-mapped `simple-bus` with two address and two size cells.
Its empty `ranges` property tells the operating system that child addresses are
system physical addresses without translation.

The `serial@10000000` child describes the fixed 0x100-byte VART virt UART
window with `compatible = "ns16550a"` and the 3.6864 MHz clock used by QEMU's
RISC-V `virt` DTB. `/chosen/stdout-path` and `/aliases/serial0` both identify
the absolute UART path so early console discovery does not depend on probing
order.

The UART carries the two-cell APLIC specifier for level-high source 10 and an
`interrupt-parent` reference to the S-mode APLIC.

## QEMU relationship

The bus properties follow `hw/riscv/fdt-common.c`, while the UART node and
console references follow `create_fdt_uart()` in `hw/riscv/virt.c` from the
local QEMU v11.1.0-rc3 tree. Addresses and sizes come from VART's shared virt
machine map rather than duplicated FDT constants.

## Validation

The machine FDT unit test independently walks the binary tree and checks the
simple-bus identity mapping, UART compatibility, 64-bit register cells, clock,
alias, and chosen console path. Existing UART unit and KVM guest tests continue
to validate the device's register and transmit behavior.
