# Initial architecture

## Priorities

The implementation order is correctness, observability, and then performance.
The first Linux machine has one RV64 vCPU, contiguous RAM, OpenSBI, a UART,
and RISC-V AIA interrupt delivery. It boots an initramfs and has no emulated
block or network device.

The architecture must nevertheless allow a later high-performance path without
changing device semantics.

## Subsystems

```text
command line and machine construction
                  |
               VM core
       +----------+-----------+
       |          |           |
     vCPU   address space   AIA/IRQ
                  |
       +----------+-----------+
       |                      |
  platform bus          PCIe host bridge
       |                +-----+------+---- future buses
  MMIO devices          |            |
                  PCIe endpoint   PCIe bridge
                                      |
                                PCIe endpoint
       |                      |
       +---- device API ------+
                  |
         event and I/O backend
```

The intended source boundaries are:

- `kvm`: `/dev/kvm` capability discovery and common ioctl wrappers
- `vm`: VM lifetime and machine construction
- `vcpu`: vCPU creation, register setup, and `KVM_RUN` handling
- `memory`: guest physical memory regions and checked guest address access
- `loader`: OpenSBI, Linux, initramfs, and device-tree placement
- `address-space`: checked registration and dispatch of RAM, ROM, MMIO, and
  alias regions, independent of any particular bus
- `platform-bus`: fixed-address platform devices described by the device tree
- `pci`: PCIe host bridge, ECAM configuration space, topology, BAR allocation
  and mapping, and PCI capability handling
- `aia`: KVM AIA irqchip setup, APLIC sources, IMSIC delivery, and routing
- `device`: lifecycle and explicit MMIO/IRQ/DMA interfaces
- `event`: replaceable timers, file-descriptor readiness, and deferred work

## Address spaces and buses

The guest physical address space is not itself a bus. It maps ranges to RAM,
ROM, or I/O callbacks and resolves overlap by explicit priority rules. A bus
owns enumeration, addressing, and device lifecycle, then publishes the regions
needed by its devices into an address space.

The initial platform bus supports fixed MMIO devices from the machine
description. Its APIs must not assume that every device has a fixed guest
physical address.

The PCIe implementation will add a host bridge and a topology of buses,
bridges, and functions. The design must cover:

- PCI Express ECAM configuration accesses and legacy-compatible configuration
  semantics where needed
- type 0 and type 1 configuration headers and multifunction devices
- 32-bit and 64-bit, prefetchable and non-prefetchable BARs
- BAR sizing probes, relocation, and address-space remapping
- PCI-to-PCI bridges and recursive bus numbering
- INTx routing through the platform interrupt controller
- MSI and MSI-X delivery to IMSIC interrupt files
- DMA through checked guest-memory accessors, with an IOMMU translation hook
- reset, attach, detach, and eventual hotplug lifecycle operations

The first Linux boot does not require a PCIe endpoint, so implementation may
start with the platform bus. However, no device or address-space API may encode
single-bus assumptions. Virtio devices should eventually support both
virtio-mmio and virtio-pci transports over a shared virtio core.

## Event and I/O evolution

The first event backend may use a single-threaded `poll` or `epoll` loop.
Devices register event interests and callbacks through a project-owned API.
They do not call `poll`, GLib, or io_uring directly.

Later backends may add io_uring, worker threads, or coroutine scheduling. The
event API should eventually express:

- file-descriptor readability and writability
- monotonic timers
- completion callbacks
- deferred work and cross-thread notification
- cancellation and device teardown

Guest-visible state changes remain serialized by the VM execution context until
a measured workload justifies more concurrency. This avoids introducing QEMU's
locking and AioContext complexity before it is required.

## QEMU device reuse

QEMU device implementations are semantic references and may also be source for
adaptation. They cannot be dropped into this project unchanged because they
depend on QOM, QEMU memory regions, IRQ objects, timers, migration state, and
other infrastructure.

When adapting a device:

1. Identify its guest-visible registers, reset state, interrupt conditions, and
   DMA behavior.
2. Preserve applicable copyright and license notices and record the upstream
   path and revision.
3. Replace QEMU infrastructure calls with the small rvmm-lab device interfaces.
4. Initially omit migration, hotplug, tracing, and compatibility versions unless
   the Linux guest requires them.
5. Compare behavior against QEMU with focused register-level and boot tests.

This approach reuses mature device behavior without importing QEMU's complete
object and event infrastructure.

## RISC-V interrupt model

The supported machine uses AIA rather than the legacy PLIC path:

- wired device interrupts enter an APLIC interrupt domain;
- MSI-capable devices target guest IMSIC interrupt files;
- KVM in-kernel AIA support is preferred when the host exposes it;
- userspace keeps an explicit interrupt-routing model and fails with a clear
  diagnostic when required KVM capabilities are absent.

The exact KVM device attributes and register initialization must be taken from
the installed kernel UAPI and checked against QEMU's current RISC-V KVM code.
