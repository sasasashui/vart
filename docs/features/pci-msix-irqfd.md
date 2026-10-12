# PCI MSI-X irqfd backend

`VartPciMsixIrqfd` connects the existing PCI MSI-X injector callback to KVM
irqfd without changing PCI or virtio device code. Each vector owns one
eventfd and one GSI. The backend lazily reads the guest-programmed MSI-X
address and data on the first delivery, adds that route to a shared
`VartKvmIrqRouting`, commits the route, assigns the eventfd, and signals it.

Lazy setup is required because a PCI function is created before the guest
programs its MSI-X table. The backend currently assumes the table is stable
after the first delivery of a vector; a later table rewrite will be handled by
the routing-update work. Destroying the backend deassigns every eventfd,
removes its routes from the shared table, and commits the remaining table.

`tests/integration/kvm/msix-irqfd.c` creates a PCI MSI-X function behind the
VART AIA machine, programs one vector, delivers it through irqfd, and checks
the complete teardown path.
