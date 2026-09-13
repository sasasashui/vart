# VART

`VART` is a small, learning-oriented RISC-V virtual machine monitor. It
uses Linux KVM for vCPU execution and implements only the userspace machine
model needed to boot a RISC-V Linux guest.

The project intentionally starts smaller than QEMU's `virt` machine. QEMU and
Linux sources beside this repository are reference implementations, not build
dependencies.

## Development environment

The initial server environment provides:

- RISC-V 64-bit host with a usable `/dev/kvm`
- QEMU source in `../qemu`
- OpenSBI dynamic firmware in
  `../qemu/pc-bios/opensbi-riscv64-generic-fw_dynamic.bin`
- Linux images in
  `../linux-build-6.18.3/arch/riscv/boot/Image` and
  `../linux-build-7.3-rc2/arch/riscv/boot/Image`
- initramfs in `../rootfs.cpio.gz`

These paths are development defaults only. Guest artifacts will eventually be
selectable through command-line options.

## Build and probe KVM

```sh
make
make check
```

The first milestone is deliberately small: `build/vart --probe` verifies that
the process can open KVM and that the kernel exposes the expected KVM API.

## Planned milestones

1. Discover RISC-V KVM vCPU registers and capabilities.
2. Create a VM, map guest RAM, and run a tiny test guest.
3. Add an MMIO bus and a minimal 16550-compatible UART.
4. Load OpenSBI, Linux, initramfs, and a generated device tree.
5. Boot one RV64 vCPU to an interactive initramfs shell.
6. Add interrupt-controller support and then virtio-mmio devices.

The first Linux target is a single vCPU with RAM, a serial console, and an
initramfs. Block, network, and SMP support are intentionally deferred.
