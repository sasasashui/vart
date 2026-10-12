# PCIe root complex

VART models the PCIe host bridge as a `VartPciRootComplex` device attached to
the system bus.  It owns one root bus, `pcie.0`, with bus number zero.  The
root bus keeps the parent system bus and the address space used by future ECAM
and PCIe MMIO windows.

The root complex exposes the standard RISC-V virt ECAM window at
`0x30000000` with a 256 MiB size.  The first configuration-space
implementation supports bus zero, static functions, 4 KiB configuration
spaces, and absent-function reads as all ones.  The root bus reserves the
RISC-V virt PCIe MMIO window at `0x40000000` with a 1 GiB size.  PCI devices
can expose 32-bit or 64-bit MMIO BARs; BARs are aligned and mapped when a
device is realized, and configuration writes support size probing and
relocation.  A device may also provide configuration-space callbacks for
device-specific registers.  The common header and BAR registers remain
handled by the PCI core, while a callback can return `-ENOENT` to delegate an
access back to the generic configuration-space storage.

MSI-X interrupt delivery and irqfd integration are added in later stages.

The capability framework allocates conventional capabilities from the
standard `0x40`-`0xff` area, maintains the header's capability pointer and
the linked-list next pointers, and sets the status register's capabilities
list bit.  Capability payloads remain part of the normal ECAM configuration
space and can therefore be accessed by the device-specific configuration
callbacks.

The MSI-X framework provides the standard capability, allocates a vector
table and pending-bit array in the selected BARs, and maps both regions with
an overlay priority above the device BAR.  Devices can connect each vector to
a `VartIrq` route.  Raises are delivered as pulses when enabled and unmasked;
otherwise the vector is recorded in the pending-bit array and delivered when
the relevant mask is cleared.  Host irqfd/eventfd acceleration is left for a
later interrupt-subsystem integration stage.

For RISC-V AIA machines, an MSI-X device can decode each table entry's IMSIC
address and interrupt data and inject it through `KVM_SIGNAL_MSI`.  The
adapter validates the target vCPU IMSIC file and interrupt ID before issuing
the KVM request.

## Bus hierarchy foundation

`VartPciBus` now tracks an optional parent PCI bus and child buses.  The
`vart_pci_bus_init()`, `vart_pci_bus_destroy()`, and `vart_pci_bus_find()`
helpers validate parent links, reject duplicate bus numbers in a subtree, and
preserve the secondary and subordinate bus-number fields.  ECAM dispatch
recursively finds functions on visible child buses, and bridge configuration
updates the advertised subordinate range through the ancestor hierarchy.

The ECAM window is described in the VART virt device tree and remains
compatible with the existing Linux boot contract.  This increment only adds
the in-memory hierarchy and bridge foundation; the default virt machine device
topology remains unchanged unless `--pcie-switch` is selected.

## Minimal PCI-to-PCI bridge

`VartPciBridge` provides a type 1 configuration header and owns one secondary
bus.  Its primary, secondary, and subordinate bus numbers are exposed through
the standard bridge registers.  ECAM lookup now recursively finds realized
functions on child buses, so a downstream endpoint can be discovered through
the root complex.  Updating the secondary bus number also updates the in-memory
bus tree after validating the number range and uniqueness.

Bridge forwarding windows, downstream BAR allocation, and switch-specific
configuration are intentionally minimal.  Each bridge currently reserves one
256 MiB non-prefetchable MMIO window from its parent bus, exposes the Type 1
Memory Base/Limit registers, and allocates downstream endpoint BARs from that
window.  Linux may probe or reprogram the Type 1 memory window while assigning
resources; VART accepts those writes while retaining the fixed host mapping.
Window relocation and 64-bit bridge windows remain deferred.

Multiple bridges may be nested.  Creating a bridge on a secondary bus assigns
the next bus number, propagates the deepest subordinate number through the
ancestor bridges, and keeps ECAM routing valid only for buses within the
advertised hierarchy.  This provides the multi-level foundation needed by a
future switch model without adding switch-specific objects yet.

## Minimal PCIe switch

`VartPciSwitch` composes one upstream bridge with a configurable set of
downstream bridge ports.  The upstream port owns the switch's internal bus;
each downstream port owns a separate secondary bus and MMIO window.  The
current implementation accepts power-of-two port counts up to four and is
intended as a topology and resource-allocation foundation.  Link training,
hotplug, error containment, and switch-specific capabilities are not modeled.

The virt machine can opt into this topology with `--pcie-switch`.  In that
mode the virtio-blk function is placed behind downstream port zero and a
virtio-net function, when present, uses downstream port one.  The option is
disabled by default so existing guest bus numbering remains unchanged.

The Linux integration checks `make check-linux-6.18.3-pcie-switch` and
`make check-linux-7.3-rc2-pcie-switch` create a temporary block image and boot
the existing Linux 6.18.3 and 7.3-rc2 guests.  They verify the switch's
upstream and downstream bridge functions and the virtio-blk endpoint through
`/sys/bus/pci`.
