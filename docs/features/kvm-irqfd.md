# KVM irqfd

The KVM irqfd wrapper binds a `VartEventNotifier` to an already configured KVM
GSI route. `vart_kvm_irqfd_assign()` installs the eventfd binding and
`vart_kvm_irqfd_deassign()` removes it. The wrapper validates the VM capability
and notifier lifetime, then submits `KVM_IRQFD` through the VM ioctl boundary.

irqfd is intentionally separate from PCI and virtio devices. A future MSI-X
injector can signal the notifier without knowing whether the backend is a
userspace injector or a KVM irqfd. The current implementation does not alter
the existing injector selection and does not use resamplefd; level-triggered
resampling will be added only when a device needs it.

`tests/integration/kvm/irqfd.c` creates the AIA machine, installs an MSI route,
assigns an eventfd to the route, signals it once, and deassigns it.
