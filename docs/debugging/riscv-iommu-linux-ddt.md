# RISC-V IOMMU Linux DDT debugging

## Symptoms

When PCI requester contexts were bound before the guest device tree described
the IOMMU, Linux could enumerate the IOMMU but did not configure DDT entries
for virtio devices.  The host-side contexts consequently stayed in the fault
mode and the first virtqueue notification failed with `-EACCES`.

## Root causes found in VART

* The PCI host FDT node lacked `iommu-map` and the `riscv,pci-iommu` child
  node, so Linux had no IOMMU association for PCI requesters.
* DDTP mode 0 is a valid transition during Linux initialization.  Returning
  an MMIO error while switching to OFF stopped the guest before it could set
  up the DDT.
* The DDT walker used extended-format indexing and 64-byte contexts even
  though VART's capability register does not advertise `MSI_FLAT`.  Linux
  consequently programs the base 32-byte format.

## Validation

The following checks pass after the fixes:

```text
make check-linux-iommu-v6-pcie-switch-net
make check-linux-6.18.3-pcie-switch-net
make check-linux-7.3-rc2-pcie-switch-net
./build/tests/iommu-device
make check
```

The Linux IOMMU target currently validates probe and virtio initialization.
It does not claim that a real DMA request has completed; add a workload that
submits a virtio request before treating the translation path as complete.
