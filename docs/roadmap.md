# Development roadmap

## Goal and method

The current goal is to boot a RISC-V Linux guest with KVM acceleration and the
smallest practical machine model. The first machine contains RV64 vCPUs, guest
RAM, a timer, RISC-V AIA, a 16550-compatible UART, a device tree, and an
initramfs. PCIe endpoints, virtio, IOMMU, networking, storage, migration, and
hotplug are outside the first boot milestone.

Development proceeds in vertical slices. Every stage should produce an
observable result from a tiny guest or an automated host-side test. Modules are
kept separate, but abstractions should normally be introduced only when a
current slice needs them or the next known slice would otherwise require an
interface rewrite.

## Current foundation

VART can open KVM, report required capabilities, create a VM, allocate guest
RAM, register and unregister a KVM memory slot, create a vCPU, access its core
registers, and execute a tiny RV64 guest to an MMIO exit. Unit and KVM
integration tests cover this foundation.

## Stage 1: execute a vCPU (complete)

Implement vCPU lifetime, the `kvm_run` mapping, RISC-V core register access,
initial PC and register state, and a minimal run loop.

The tiny guest writes a known value to an unmapped address. The expected result
is a decoded `KVM_EXIT_MMIO` containing the correct address, size, direction,
and value. This validates instruction execution, RAM, CPU state, and VM exits.

Completed by commit `3cf4bf4` for the vCPU lifecycle and the subsequent tiny
guest execution increment. The automated test verifies the full MMIO exit
record on the RISC-V KVM server.

## Stage 2: address space and MMIO dispatch (complete)

Implement first-class address spaces and RAM, ROM, MMIO, and alias regions.
Add checked region registration, overlap handling, access-size validation, and
read/write dispatch. CPU accesses use the system address space; the design must
not assume a single bus or a single global address space.

Tests cover 1, 2, 4, and 8-byte MMIO operations, boundaries, overlaps,
unmapped accesses, and callback failures.

## Stage 3: test device and polling UART (complete)

Add a VART-only test device for character output, explicit pass/fail status,
and clean guest termination. This device becomes the observable endpoint for
later tiny guests.

Then adapt the smallest useful 16550 UART behavior from QEMU. Initially support
polling transmit only. A tiny guest must print a deterministic message through
both devices.

## Stage 4: multiple vCPUs

Run each vCPU in its own host thread under the VM big lock. Implement vCPU
start, stop, join, kick, shutdown coordination, and secondary-hart state.

Tiny guests validate shared RAM, atomic operations, concurrent MMIO, secondary
hart wakeup, and clean shutdown. Correctness and deadlock detection take
priority over parallel performance.

The stage is split into reviewable increments:

1. VM big lock, synchronization wrappers, and optional debug-lock checking
   (complete)
2. vCPU thread lifecycle and single-vCPU threaded execution (complete)
3. vCPU kick and coordinated VM shutdown (complete)
4. multiple vCPUs with shared-RAM atomic synchronization (complete)
5. serialized concurrent MMIO under the VM big lock (complete)
6. secondary-hart start and stop state (complete)

## Stage 5: RISC-V KVM boot contract (complete)

Determine and document the exact privilege, register, and SBI contract used by
the server kernel and current QEMU KVM implementation. QEMU supports only direct
S-mode kernel boot with KVM; VART does not load OpenSBI in this path.

Tiny guests validate hart ID, initial PC, `a0` and `a1`, supported SBI
extensions, timer calls, IPIs, system reset, and debug console where available.

The stage is split into reviewable increments:

1. RISC-V KVM boot capability discovery (complete)
2. direct-boot register contract (complete)
   - complete CPU register state and synchronization framework (complete)
   - direct-boot state initialization (complete)
3. SBI BASE extension (complete)
4. SBI TIME, IPI, and RFENCE extensions (complete)
5. SBI HSM extension (complete)
6. SBI system reset and userspace exits (complete)
   - SRST and KVM system-event decoding (complete)
   - userspace SBI exit dispatch (complete)
7. consolidated direct-boot contract (complete)

## Stage 6: device tree

Generate a minimal device tree describing CPUs, RAM, chosen boot parameters,
initramfs, UART, timer, and AIA topology. Validate it structurally with device
tree tools, compare important properties with QEMU `virt`, and let a tiny guest
check the FDT header passed in `a1`.

The stage is split into reviewable increments:

1. FDT binary builder and format validation (complete)
2. CPU and memory nodes (complete)
3. chosen node and boot-resource layout (complete)
4. SoC and UART nodes (complete)
5. AIA interrupt topology (complete)
6. complete machine DTB integration (complete)

## Stage 7: AIA interrupt delivery (complete)

Build interrupt support as independently testable paths:

1. KVM AIA device lifecycle and mode selection (complete).
2. IMSIC injection to one vCPU (complete).
3. IMSIC delivery to a selected vCPU in an SMP guest (complete).
4. One APLIC wired interrupt source (complete).
5. APLIC-to-IMSIC delivery (complete).
6. Device-to-APLIC delivery (complete).
7. UART receive interrupt delivery (complete).

Each path gets a focused guest under `tests/guests/aia/` or the relevant device
subdirectory before it is used for Linux diagnosis.

## Stage 8: first Linux boot

The stage is split into reviewable increments:

1. Complete virt machine assembly and lifecycle (complete).
2. Linux kernel and initramfs file loading (complete).
3. Command-line boot entry and runtime diagnostics (complete).
4. Linux 6.18.3 early boot (complete).
5. Linux AIA and UART driver initialization (complete).
6. Initramfs `/init` and shell prompt (complete).
7. Repeatable boot regression and cleanup testing (complete).
8. Linux 7.3-rc2 and SMP validation (complete).

Start with Linux 6.18.3, one vCPU, polling console output, and the existing
initramfs. Track progress through observable checkpoints:

1. Linux banner.
2. CPU discovery.
3. Memory discovery.
4. Timer initialization.
5. AIA initialization.
6. UART initialization.
7. Initramfs unpacking.
8. `/init` execution.
9. Shell prompt.

After this path is stable, repeat it with Linux 7.3-rc2 and then enable SMP.

## Stage 9: interactive console and event backend

Add terminal raw mode, nonblocking input, UART receive state, receive
interrupts, signal handling, and coordinated shutdown. Introduce a small
project-owned event API backed initially by `poll` or `epoll`. Devices must not
depend directly on the chosen host event mechanism.

The stage completes when the initramfs shell accepts input and exits cleanly.

## Later evolution

After the minimum Linux machine is reliable, extend it in measured increments:

- PCIe host bridge, ECAM, bridges, BARs, INTx, MSI, and MSI-X
- a shared virtio core with virtio-mmio and virtio-pci transports
- per-device DMA address spaces and RISC-V IOMMU translation
- block and network backends
- replaceable asynchronous I/O, worker, io_uring, and coroutine backends
- finer locks for measured contention under the established lock order

These future requirements constrain interfaces now, but they do not justify
implementing unused infrastructure before the first Linux boot.

## Completion criteria for an increment

A critical increment is complete only when:

- responsibilities are placed in the appropriate modules;
- it produces a deterministic observable result;
- normal, boundary, and important error paths have automated coverage;
- relevant tiny guests and tests are connected to `make check`;
- architecture and feature documentation remain accurate;
- difficult debugging knowledge is recorded when applicable;
- `make check` and `git diff --check` pass;
- one coherent English Linux-style commit is created; and
- the worktree is clean.
