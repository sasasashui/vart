# virtio-net

VART now has a modern virtio-net PCI device model with a fixed MAC address,
link status, two virtqueues, and three MSI-X vectors. A backend file descriptor
can be attached directly or opened as a TAP device. The TX queue consumes the
virtio-net header and sends guest Ethernet frames to that backend. A backend
can be registered with a `VartEventLoop`; readable frames are then copied into
the guest RX queue and signalled with the queue interrupt.

The backend descriptor is owned by the device after
`vart_virtio_net_attach_backend()` or `vart_virtio_net_open_tap()` succeeds and
is closed by `vart_virtio_net_destroy()`.

`vart_virtio_net_attach_event_loop()` and
`vart_virtio_net_detach_event_loop()` control backend polling. The device uses
a private lock to serialize event-loop callbacks with virtqueue notifications.

The machine frontend enables the device with:

```text
--virtio-net-tap IFNAME
```

This opens `/dev/net/tun` as a TAP backend and registers it with the runtime
event loop. The option requires the host permissions needed to create or join
the requested TAP interface.

For tests and unprivileged backends, an already-open descriptor can be passed
without changing host network configuration:

```text
--virtio-net-fd FD
```

VART takes ownership of this descriptor after machine creation and closes it
when the machine is destroyed. `--virtio-net-tap` and `--virtio-net-fd` are
mutually exclusive.
