# virtio-net

VART now has a modern virtio-net PCI device model with a fixed MAC address,
link status, two virtqueues, and three MSI-X vectors. The notify callback is
currently a no-op; TAP-backed RX/TX processing will be added in a later step.
