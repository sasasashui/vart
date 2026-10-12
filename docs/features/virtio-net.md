# virtio-net

VART now has a modern virtio-net PCI device model with a fixed MAC address,
link status, two virtqueues, and three MSI-X vectors. A backend file descriptor
can be attached directly or opened as a TAP device. The TX queue consumes the
virtio-net header and sends guest Ethernet frames to that backend. RX polling
and event-loop integration are intentionally deferred to a later step.

The backend descriptor is owned by the device after
`vart_virtio_net_attach_backend()` or `vart_virtio_net_open_tap()` succeeds and
is closed by `vart_virtio_net_destroy()`.
