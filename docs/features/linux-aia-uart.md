# Linux AIA and UART

Linux 6.18.3 discovers the supervisor IMSIC at `0x28000000`, configures 255
identities, and uses identity 1 for IPIs. It then configures the supervisor
APLIC at `0x0d000000` with 96 wired sources forwarded to the IMSIC. The
NS16550A at `0x10000000` binds to ttyS0 and uses the DT level-high interrupt
source 10; Linux reports the allocated Linux IRQ number separately.

The focused integration test boots the real kernel and initramfs without a
host event loop. After the shell prompt appears, it queues a command through
the console frontend. The frontend feeds the UART while RBR has space and
retains the remaining bytes under backpressure. Every delivered byte asserts
UART source 10 through `KVM_IRQ_LINE`; KVM APLIC converts it to an MSI for the
IMSIC, and Linux reads RBR before the frontend drains the next byte. The shell
command prints unique markers and powers off through SBI SRST.

This test proves driver-level receive interrupt delivery but is not an
interactive console implementation. Stage 9.5 will connect nonblocking host
stdin to this console queue through the event backend.
