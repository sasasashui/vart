# QEMU virt IMSIC omits required MSI cells

## Context

The discrepancy was found while implementing the VART Stage 6.5 AIA tree.
The references were QEMU commit `3e3ccab106f8` (v11.1.0-rc3 development tree)
and Linux commit `e9d0973a72a4`.

## Symptoms

QEMU's dumped `virt,aia=aplic-imsic` DTB contains `msi-controller` on the
S-mode IMSIC node but no `#msi-cells` property. The Linux
`riscv,imsics.yaml` binding lists `#msi-cells` as required with value zero.

## Reproduction

Generate a two-vCPU KVM tree with QEMU's `dumpdtb` machine option and inspect
its string block. `msi-controller` is present while `#msi-cells` is absent.
The same omission is visible in `create_fdt_one_imsic()`.

## Investigation

The other required IMSIC properties are emitted, and the omission is
independent of vCPU count. Linux currently discovers the controller using the
remaining properties, so this is primarily a schema-conformance problem rather
than the cause of a demonstrated boot failure.

## Root cause

QEMU creates the IMSIC as an MSI controller without emitting the zero-cell MSI
specifier declared by the current Devicetree binding.

## Fix

VART emits `#msi-cells = <0>` and treats the Linux binding as authoritative.
An upstream QEMU change can add the same property independently.

## Regression coverage

`tests/unit/fdt/virt-machine.c` requires the property in VART's binary DTB.
