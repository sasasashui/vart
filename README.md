# VART

VART is a small, learning-oriented RISC-V virtual machine monitor. It uses
Linux KVM for CPU execution and implements the userspace machine model needed
to boot a RISC-V Linux guest without carrying QEMU's multi-architecture and
binary-translation scope.

Version 0.1.0 boots Linux 6.18.3 and Linux 7.3-rc2 to an interactive initramfs
shell. The supported machine provides RV64 vCPUs, contiguous RAM, a generated
device tree, an NS16550A UART, and an in-kernel RISC-V AIA composed of APLIC and
IMSIC. Multiple vCPUs run in separate host threads under a VM-wide big lock.

KVM enters the kernel directly in supervisor mode. VART does not load OpenSBI;
the host KVM implementation provides the SBI services required by the guest.
QEMU and Linux source trees beside this repository are reference
implementations, not build dependencies.

## Build

The host must be RV64 Linux with RISC-V KVM and a toolchain capable of building
the small RV64 test guests:

```sh
make -j$(nproc)
make check
make check-debug-locks
```

`make check` runs host unit tests, tiny RISC-V guests, and KVM integration
tests. The debug build enables VART's lock ownership and ordering checks.

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

The 0.1.0 machine deliberately has no PCIe host bridge, virtio devices, IOMMU,
block backend, network backend, migration, or hotplug. Its current event loop
uses `poll`, UART output is synchronous, and guest-visible shared state is
serialized by the big lock. The address-space, device, DMA, interrupt, and
event interfaces preserve boundaries needed to add those facilities later.

See `docs/design.md` for the architecture, `docs/roadmap.md` for completed
development stages, `docs/features/` for subsystem contracts, and
`docs/debugging/` for reusable investigation records.
