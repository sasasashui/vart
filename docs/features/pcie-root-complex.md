# PCIe root complex

VART models the PCIe host bridge as a `VartPciRootComplex` device attached to
the system bus.  It owns one root bus, `pcie.0`, with bus number zero.  The
root bus keeps the parent system bus and the address space used by future ECAM
and PCIe MMIO windows.

This stage establishes topology and lifecycle only.  PCI configuration-space
access, BAR assignment, capabilities, and MSI-X are added in later stages.
The root complex currently has no guest-visible MMIO region and therefore does
not change the existing Linux device tree or boot contract.
