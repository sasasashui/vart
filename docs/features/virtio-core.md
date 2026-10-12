# Virtio split-queue core

Stage 11.3 adds the transport-independent virtio core used by future
virtio-pci devices.

The core owns device feature negotiation, status, and queue lifecycle. Each
queue uses a pair of DMA callbacks, so the transport can provide a PCI device's
`dma_as` today and an IOMMU-translated address space later without changing
descriptor processing.

The initial queue implementation supports split virtqueues with chained
descriptors. It validates queue alignment, power-of-two size, ring bounds,
descriptor indices, descriptor flags, and loops. Indirect descriptors are
explicitly rejected until a transport/device needs them. Used entries are
written in little-endian format before the used index is published.

`tests/unit/virtio/core.c` exercises feature negotiation, queue lifecycle,
descriptor-chain walking, malformed-chain rejection, and used-ring updates
using a bounded in-memory DMA backend.
