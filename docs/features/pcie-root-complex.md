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

The ECAM window is not advertised in the guest device tree yet, so this stage
does not change the existing Linux boot contract.
