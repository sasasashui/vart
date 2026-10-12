# KVM interrupt routing

`VartKvmIrqRouting` owns the userspace MSI route table submitted through
`KVM_SET_GSI_ROUTING`. The wrapper stores VART-owned route descriptions and
builds the Linux KVM ABI structure only when committing the table, keeping
the ioctl and its variable-sized allocation in one implementation boundary.

The initial interface supports MSI routes with a GSI, 64-bit message address,
data, and optional device identifier. Routes can be appended, cleared, and
committed again. The VM must have an in-kernel interrupt controller before
KVM accepts the table; VART reports `-ENOTSUP` when the host lacks the
required KVM routing or signal-MSI capabilities.

This layer does not yet allocate GSIs or attach eventfds. Those responsibilities
belong to the later irqfd integration. Existing userspace MSI-X injectors and
UART APLIC wiring are unchanged.

`tests/integration/kvm/irq-routing.c` creates the VART AIA machine, submits an
MSI route, clears it, and submits the empty table again.
