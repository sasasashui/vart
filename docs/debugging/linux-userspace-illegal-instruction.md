# Linux init dies on a valid floating-point instruction

## Symptom

Linux 6.18.3 booted through `/init`, then BusyBox PID 1 received signal 4 at
offset `0xce44a`. The kernel reported exception cause 2 and panicked after
killing init. Disassembly bytes contained a compressed floating-point
instruction.

## Investigation

KVM reported F, D, and C support and the same BusyBox ran under the reference
environment. VART's runtime-generated CPU node, however, advertised only IMA
plus Zicsr, Zifencei, SSTC, and SSAIA. Linux therefore did not enable and manage
floating-point state for userspace even though KVM could execute it.

## Root cause and fix

The runtime CPU ISA description under-reported enabled KVM features. Advertising
F, D, and C in both `riscv,isa` and `riscv,isa-extensions` lets Linux establish
the matching userspace execution state. BusyBox then runs past the failing
instruction and reaches its startup banner.

## Regression coverage

The Linux early-boot smoke test rejects kernel panic output. The longer boot
used during diagnosis additionally verified initramfs unpacking and `/init`.
The fixed ISA list is suitable for the current server; deriving all extensions
from KVM remains future CPU-model work.
