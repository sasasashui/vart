# Linux 6.18.3 early boot

VART boots the locally built Linux 6.18.3 `Image` directly in S-mode with one
vCPU, 512 MiB RAM, the generated DTB, and the existing compressed initramfs.
The KVM-provided timer frequency is 24 MHz on the development server.

The verified checkpoints are the Linux banner, VART machine model, SBI TIME,
CPU ISA discovery, the 512 MiB memory range, S-mode SSTC timer, one online CPU,
and the final memory summary. The same run also reaches AIA discovery, unpacks
the initramfs, switches from earlycon to ttyS0, and invokes `/init`; those later
subsystems receive their own stage coverage.

The runtime DT currently describes the F, D, and C extensions enabled by the
development server in addition to the required IMA, Zicsr, Zifencei, SSTC, and
SSAIA extensions. This is necessary because the supplied BusyBox uses floating
point and compressed instructions. Future heterogeneous or configurable CPU
models must derive the complete string from KVM rather than use this fixed
host-compatible description.

`make check-linux-6.18.3` runs a five-second smoke test using external Linux
build artifacts, verifies the early-boot markers, and rejects panic, Oops, or
BUG output. It remains separate from `make check` so normal builds do not
depend on sibling kernel and rootfs files.
