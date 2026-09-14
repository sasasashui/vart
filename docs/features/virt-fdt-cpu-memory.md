# Virt CPU and memory device tree

## Purpose

The RISC-V virt machine describes its vCPUs and contiguous RAM with a DTB
built on the bounded format layer. This is the first machine-policy layer over
the generic FDT builder; later Stage 6 increments extend the same configuration
and generator with boot resources and devices.

## Configuration

`VartVirtFdtConfig` supplies the hart list, KVM timebase frequency, and RAM
range. Each hart carries its guest-visible hart ID, legacy ISA string, ISA base
and extension list, and maximum MMU mode. These CPU properties are inputs
rather than constants because a KVM VM must describe the architectural state
exposed by its host vCPU model.

The generator rejects an empty CPU set, zero timebase or RAM size, a wrapping
RAM range, incomplete CPU strings, and duplicate hart IDs. The returned blob
is owned by the caller.

## Guest-visible layout

The root uses two address and two size cells and remains compatible with the
QEMU RISC-V `virt` binding. `/cpus` uses one-cell hart IDs and contains one
`cpu@<hartid>` node per configured vCPU. CPU nodes provide `device_type`,
`reg`, `status`, `compatible`, `riscv,isa`, `riscv,isa-base`,
`riscv,isa-extensions`, and `mmu-type`. The `memory@<base>` node encodes both
address and size as 64-bit cell pairs, so RAM above or larger than 4 GiB is
represented without truncation.

CPU interrupt-controller children and phandles belong to the later AIA
topology increment and are deliberately absent here.

## QEMU relationship

The layout follows `hw/riscv/fdt-common.c` and `hw/riscv/virt.c` in the local
QEMU v11.1.0-rc3 reference tree. VART keeps CPU discovery separate from FDT
serialization instead of depending on QEMU CPU objects or libfdt.

## Validation

The unit test generates a two-hart tree with non-contiguous hart IDs and 4 GiB
of RAM. It walks the binary tokens independently, checks all node properties
and 64-bit cells, writes the DTB under `build/tests/`, and covers invalid and
duplicate configuration inputs.
