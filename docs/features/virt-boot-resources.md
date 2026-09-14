# Virt boot resources

## Purpose

The virt boot layout keeps the raw RV64 Linux image, optional initramfs, and
DTB in disjoint ranges of contiguous guest RAM. The resulting initramfs range
and command line are described in `/chosen` while the DTB address is passed to
the primary vCPU through `a1` during later machine integration.

## Layout

The kernel starts at the first 2 MiB boundary in RAM, as required by the RV64
Linux boot protocol. Following QEMU's RISC-V policy, an initramfs starts halfway
through RAM on machines smaller than 1 GiB and 512 MiB after the kernel start
on larger machines. The final, 2 MiB-aligned RAM area is reserved for the DTB,
matching Linux's maximum early FDT mapping size.

The layout operation is transactional: it publishes no partial result after a
failure. It rejects wrapping ranges, insufficient RAM, and overlap between the
kernel, initramfs, and reserved DTB area. The integrated loader validates and
builds all resources before writing the kernel, optional initramfs, and DTB to
RAM. It returns the kernel entry and DTB address in the form consumed by vCPU
direct-boot initialization.

## Chosen node

The machine DTB always contains `/chosen`. A non-empty command line becomes
the string property `bootargs`. When an initramfs is present,
`linux,initrd-start` and `linux,initrd-end` are emitted as 64-bit values. The
FDT generator rejects incomplete, wrapping, or out-of-RAM initramfs ranges.

## QEMU and Linux relationship

The initramfs and DTB placement policy follows `hw/riscv/boot.c` from the local
QEMU v11.1.0-rc3 tree. Kernel and FDT alignment requirements follow
`Documentation/arch/riscv/boot.rst` and `arch/riscv/include/asm/pgtable.h` in
the local Linux tree.

## Validation

The layout unit test covers machines below, at, and above the QEMU 1 GiB
threshold, boot without an initramfs, alignment, arithmetic overflow, and every
resource-overlap boundary. It also checks complete RAM placement, returned boot
state, invalid image pointers, and failure atomicity. The machine FDT test
independently checks the binary `/chosen` properties and invalid initramfs
descriptions.
