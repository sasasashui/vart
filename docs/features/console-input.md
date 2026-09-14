# Console input queue

The console frontend owns a bounded 4096-byte ring between a host input
backend and the NS16550A receiver. Host reads enqueue as much data as space
allows. A full queue returns `EAGAIN`, allowing the event owner to disable
input readiness until the guest consumes data instead of dropping bytes or
growing memory without a limit.

`vart_console_drain_input()` presents queued bytes to
`vart_uart16550_receive()` in order. The current UART accepts one byte into RBR
and then applies backpressure until the guest reads it. The frontend retains
the rest of the queue and can be drained again after the MMIO read. UART or
interrupt-delivery errors leave the rejected byte at the head of the queue.

Queue operations and UART state changes are deliberately lockless. The
production console serializes them with the VM big lock when connected to
vCPU execution. This keeps lock ownership out of the device and frontend and
permits finer-grained locking later.
