# KVM ioeventfd

VART wraps `KVM_IOEVENTFD` in `vart_kvm_ioeventfd_assign()` and
`vart_kvm_ioeventfd_deassign()`. The wrapper binds a host eventfd to a guest
MMIO address without exposing the KVM ioctl to device code.

The modern virtio PCI transport registers one ioeventfd per queue after its
notify BAR has been mapped. The runtime event loop consumes those eventfds and
dispatches the corresponding queue notification while holding the VM lock.
When KVM does not provide ioeventfd, the existing userspace MMIO callback
remains the fallback path.

Bindings are created after BAR assignment and are torn down before the device
is destroyed. BAR relocation while the binding is active is not supported yet.
