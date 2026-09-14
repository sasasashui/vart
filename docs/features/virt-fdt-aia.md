# Virt AIA device tree

## Purpose

The virt DTB describes the supervisor-level Advanced Interrupt Architecture
used by a KVM Linux guest. Wired platform sources enter an APLIC domain, which
forwards them as MSIs to one IMSIC interrupt file per vCPU.

## Topology and phandles

Every CPU node owns a `riscv,cpu-intc` child. Its phandle and supervisor
external interrupt number form one `interrupts-extended` entry in the IMSIC
node. Phandles are assigned deterministically in CPU order, followed by the
IMSIC and APLIC phandles, and duplicate hart IDs remain invalid.

The S-mode IMSIC begins at `0x28000000`. With no VS guest interrupt files, each
vCPU occupies one 4 KiB file and the single-group `reg` size scales with the
vCPU count. It exposes 255 interrupt identities and acts as both an interrupt
and MSI controller.

The S-mode APLIC begins at `0x0d000000`, exposes 96 wired sources in a 0x8000
window, and references the IMSIC through `msi-parent`. The UART targets APLIC
source 10 with the standard level-high trigger flag.

Only supervisor-level nodes are described because a KVM direct-boot guest
does not enter machine-mode firmware. Multiple sockets, IMSIC groups, and VS
guest files can be added when the corresponding KVM configuration exists. The
single-socket machine is capped at QEMU virt's 512 vCPUs so its IMSIC and APLIC
windows always cover every declared hart.

## QEMU and Linux relationship

Addresses, source counts, MSI identities, compatibility strings, and topology
follow `create_fdt_one_imsic()` and `create_fdt_one_aplic()` in the local QEMU
v11.1.0-rc3 tree. VART additionally emits the `#msi-cells = <0>` required by
the Linux `riscv,imsics` binding; the observed QEMU discrepancy is recorded in
`docs/debugging/qemu-imsic-missing-msi-cells.md`.

## Validation

The machine FDT unit test walks every CPU interrupt-controller, IMSIC, APLIC,
and UART interrupt property directly from the binary blob. It verifies
phandle references, per-vCPU IMSIC sizing, all cell counts, addresses, source
counts, trigger type, and the maximum single-group vCPU limit.
