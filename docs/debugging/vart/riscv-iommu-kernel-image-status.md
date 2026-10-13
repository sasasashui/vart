# Guest image IOMMU status

The VART PCIe tests do not require a guest IOMMU device.  The existing Linux
images were therefore checked separately before using them for future IOMMU
integration work.

## Linux 6.18.3 image

`linux-build-6.18.3/arch/riscv/boot/Image` contains the RISC-V IOMMU driver
configuration:

```text
CONFIG_RISCV_IOMMU=y
CONFIG_RISCV_IOMMU_PCI=y
```

The generated `include/generated/autoconf.h` does not define
`CONFIG_IOMMU_DMA`.  This image must therefore be treated as RISC-V IOMMU
driver coverage only, not as an IOMMU-DMA test kernel.

## Linux 7.3-rc2 image

The image identifies itself as:

```text
Linux version 7.3.0-rc2-00017-ge9d0973a72a4
```

Its generated configuration also contains `CONFIG_RISCV_IOMMU=y` and
`CONFIG_RISCV_IOMMU_PCI=y`, but does not define `CONFIG_IOMMU_DMA` in
`include/generated/autoconf.h`.  The source tree at `ge9d0973a72a4` contains
the v5 RISC-V IOMMU-DMA Kconfig change, while the separate
`linux-riscv-iommu-v6` tree contains the v6 series.  Neither fact changes the
configuration of this already-built Image.

Consequently, future guest IOMMU-DMA measurements must first build an Image
from the intended source tree and verify `CONFIG_IOMMU_DMA` in generated
configuration output.  The current 7.3-rc2 Image remains useful for testing
the RISC-V IOMMU driver and PCIe enumeration, but must not be described as an
IOMMU-DMA-enabled kernel.

## Linux IOMMU v6 image

`linux-build-iommu-v6/arch/riscv/boot/Image` identifies itself as
`7.3.0-rc5-g57b68d459e32`.  Its generated `.config` contains:

```text
CONFIG_IOMMU_DMA=y
CONFIG_RISCV_IOMMU=y
CONFIG_RISCV_IOMMU_PCI=y
CONFIG_IOMMUFD=m
```

This is the first local image suitable for future guest IOMMU-DMA and MSI
remapping experiments.  It still cannot exercise those paths under VART until
the guest-visible RISC-V IOMMU device is implemented; the current
`check-linux-iommu-v6-pcie-switch-net` target only validates ordinary Linux
boot and PCIe/virtio-net behavior with this kernel.
