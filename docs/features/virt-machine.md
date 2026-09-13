# RISC-V virt machine map

VART follows QEMU's RISC-V `virt` guest physical address map. Central constants
in `vart/machine/virt.h` are authoritative for devices, tests, AIA setup, PCIe,
device-tree generation, and loaders. Callers must not duplicate literal machine
addresses.

The first implemented regions are DRAM at `0x80000000` and NS16550A UART at
`0x10000000`. Reserved future windows retain QEMU addresses, including machine
and supervisor APLIC at `0x0c000000` and `0x0d000000`, IMSIC domains at
`0x24000000` and `0x28000000`, PCIe ECAM at `0x30000000`, and PCIe MMIO at
`0x40000000`.

IMSIC groups are separated by 16 MiB. A hart occupies one 4 KiB interrupt file
for each guest-file index, so its stride is `4 KiB << guest_bits`. Configuration
code must validate group, hart, and guest-bit limits before using address helpers.
