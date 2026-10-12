# PCIe root complex

VART models the PCIe host bridge as a `VartPciRootComplex` device attached to
the system bus.  It owns one root bus, `pcie.0`, with bus number zero.  The
root bus keeps the parent system bus and the address space used by future ECAM
and PCIe MMIO windows.

The root complex exposes the standard RISC-V virt ECAM window at
`0x30000000` with a 256 MiB size.  The first configuration-space
implementation supports bus zero, static functions, 4 KiB configuration
spaces, and absent-function reads as all ones.  BAR assignment, capabilities,
and MSI-X are added in later stages.

The ECAM window is not advertised in the guest device tree yet, so this stage
does not change the existing Linux boot contract.
