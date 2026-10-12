# PCI host bridge device tree

Stage 11.2 describes the VART PCIe root complex in the virt machine device
tree. The node follows the `pci-host-ecam-generic` binding used by the QEMU
RISC-V virt board:

- ECAM is exposed at `0x30000000` with a `0x10000000` window.
- The PCI bus range covers buses 0 through 255.
- PCI I/O and below-4G MMIO windows are described by `ranges`.
- `interrupt-map` targets the S-mode APLIC, using the standard four-pin swizzle
  and interrupt sources 32 through 35.
- `msi-parent` points at the AIA IMSIC node.

The current PCI implementation has one root bus, so the generated bus range
is deliberately broader than the set of currently instantiated functions.
This leaves the DT contract compatible with future bridges and hotplug-like
enumeration without changing the ECAM layout.

`tests/unit/fdt/virt-machine.c` validates the host bridge properties, ranges,
MSI parent, and every entry in the interrupt map.
