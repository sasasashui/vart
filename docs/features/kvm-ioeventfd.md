# KVM ioeventfd

VART wraps `KVM_IOEVENTFD` in `vart_kvm_ioeventfd_assign()` and
`vart_kvm_ioeventfd_deassign()`. The wrapper binds a host eventfd to a guest
MMIO address without exposing the KVM ioctl to device code.

This step only validates the KVM binding lifecycle. The virtio notify path
continues to use the normal userspace MMIO callback. A later step will consume
the eventfd from the event loop and dispatch the corresponding virtio queue
notification.
