# Virtio modern PCI transport

Stage 11.4 connects the transport-independent virtio core to the PCI root
complex using the modern virtio-pci layout.

Each device exposes one MMIO BAR containing the common configuration, queue
notifications, ISR status, and device-specific configuration windows. Four
vendor capabilities advertise those windows through the standard virtio PCI
capability format. The common configuration implements feature selection,
status, queue selection, split-queue addresses, queue enable/reset, and queue
notification.

Queue reset and device status reset also clear the transport-side queue
addresses, size, and interrupt vector state before a queue is configured again.

Queue addresses are written through the standard 32-bit low/high register
pairs. The transport passes them to the virtio core only when the queue is
enabled. The notify callback is transport-facing and receives an independent
opaque pointer, leaving device-specific work outside the transport.

MSI-X vector fields and the ISR register are represented now; interrupt
delivery remains a separate path and can be connected to the existing PCI
MSI-X implementation in a later device stage.

`tests/unit/virtio/pci.c` validates PCI identity and capability chaining,
feature/status registers, split-queue setup, notifications, and ISR
read-to-clear behavior.
