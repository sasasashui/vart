# VART

VART is a small, learning-oriented RISC-V virtual machine monitor. It uses
Linux KVM for CPU execution and implements the userspace machine model needed
to boot a RISC-V Linux guest without carrying QEMU's multi-architecture and
binary-translation scope.

The current checkpoint is version 0.3.0. It boots Linux 6.18.3 and Linux
7.3-rc2 to an interactive initramfs shell and adds a PCIe root complex, nested
PCIe buses, virtio-blk, virtio-net, PCI bus mastering, and a guest-visible
RISC-V IOMMU path. The supported machine provides RV64 vCPUs, contiguous RAM,
a generated device tree, an NS16550A UART, and an in-kernel RISC-V AIA composed
of APLIC and IMSIC. Multiple vCPUs run in separate host threads under a VM-wide
big lock.

KVM enters the kernel directly in supervisor mode. VART does not load OpenSBI;
the host KVM implementation provides the SBI services required by the guest.
QEMU and Linux source trees beside this repository are reference
implementations, not build dependencies.

## Build

The host must be RV64 Linux with `/dev/kvm`, in-kernel RISC-V AIA support, a C
toolchain, GNU make, and binutils capable of building the small RV64 test
guests. From the repository root, build the executable with:

```sh
make -j$(nproc)
```

The resulting executable is `build/vart`. Query the host KVM features required
by VART before trying to boot a guest:

```sh
build/vart --probe
```

Run the complete development test suite, including KVM integration tests, with:

```sh
make check
make check-debug-locks
```

`make check` runs host unit tests, tiny RISC-V guests, and KVM integration
tests. The debug build enables VART's lock ownership and ordering checks.

The IOMMU integration test uses the separately prepared Linux 7.3-rc5 image
with the RISC-V IOMMU DMA and MSI-remapping series:

```sh
make check-linux-iommu-v6-pcie-switch-net
```

This target is optional; the ordinary Linux boot targets use the kernel images
listed below.

## Run Linux

The development server provides these default artifacts:

- `../linux-build-6.18.3/arch/riscv/boot/Image`
- `../linux-build-7.3-rc2/arch/riscv/boot/Image`
- `../rootfs.cpio.gz`

Start the Linux 6.18.3 guest with:

```sh
build/vart \
    --kernel ../linux-build-6.18.3/arch/riscv/boot/Image \
    --initrd ../rootfs.cpio.gz \
    --memory 512M \
    --cpus 1 \
    --append "earlycon=uart8250,mmio,0x10000000 console=ttyS0"
```

`--initrd`, `--append`, `--dump-fdt`, `--memory`, `--cpus`, `--pcie-switch`,
`--riscv-iommu`, and the virtio backend options are optional. `--dump-fdt PATH`
saves the generated machine DTB before guest memory is modified, which is
useful for inspection with `dtc`. Their defaults
are no initramfs, the serial console command line shown above, 512 MiB of RAM,
and one vCPU respectively. Display the complete supported syntax with:

```sh
build/vart --help
```

Attach a raw disk image as a virtio-blk PCI function, or select the PCIe switch
and guest-visible IOMMU topology, with:

```sh
build/vart \
    --kernel ../linux-build-6.18.3/arch/riscv/boot/Image \
    --initrd ../rootfs.cpio.gz \
    --virtio-blk ../disk.img \
    --pcie-switch \
    --riscv-iommu
```

For networking, use an already-open unprivileged backend descriptor with
`--virtio-net-fd FD`, or request a TAP backend with `--virtio-net-tap IFNAME`.
The two options are mutually exclusive and the device takes ownership of the
descriptor after successful machine construction.

For example, run Linux 7.3-rc2 with two vCPUs using:

```sh
build/vart \
    --kernel ../linux-build-7.3-rc2/arch/riscv/boot/Image \
    --initrd ../rootfs.cpio.gz \
    --memory 512M \
    --cpus 2
```

Standard input and output are connected to the emulated UART. Ctrl-C requests
a coordinated VART shutdown, kicks any vCPU blocked in `KVM_RUN`, and restores
the host terminal before exiting.

Useful Linux regression targets are:

```sh
make check-linux-6.18.3
make check-linux-6.18.3-interactive
make check-linux-6.18.3-pty
make check-linux-7.3-rc2-smp
```

## Scope

The machine includes a PCIe root complex with ECAM, endpoint and bridge
configuration spaces, nested bus/switch topology, modern virtio-pci transport,
raw-file virtio-blk, and virtio-net backends. PCI bus mastering uses a device
owned DMA address space; the current RISC-V IOMMU model provides first-stage
Sv39/Sv39x4 translation, guest DDT handling, fault reporting, and the initial
MSI-remapping path.

This remains a functional and learning-oriented model. Asynchronous I/O and
io_uring backends, complete PCIe error/power/hotplug semantics, ATS/PASID and
Page Request Queue, G-stage translation, migration, VFIO, and production-grade
network backends remain future work. The current event loop uses `poll`, UART
output is synchronous, and guest-visible shared state is serialized by the big
lock.

See `docs/design.md` for the architecture, `docs/roadmap.md` for completed
development stages, `docs/features/` for subsystem contracts, and
`docs/debugging/` for reusable investigation records. The PCIe checkpoint is
recorded in `docs/releases/v0.2.0-pcie-audit.md`; current IOMMU and device
contracts are documented under `docs/features/`.
