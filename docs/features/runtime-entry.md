# Runtime entry and diagnostics

VART now accepts a direct-boot kernel with optional initramfs, boot arguments,
RAM size, and vCPU count. Memory sizes accept byte counts or one `K`, `M`, or
`G` suffix and must be page aligned. The default machine has 512 MiB RAM, one
vCPU, and an early NS16550A console on the virt UART.

The runtime validates the RISC-V KVM boot contract, reads the KVM timer
frequency for the DTB, loads all boot resources, and starts every machine
vCPU. Standard SBI services remain in KVM. MMIO exits enter the system address
space; system events and shutdown exits stop all vCPU workers. Unexpected
userspace SBI and unknown KVM exits terminate the machine with the hart ID,
KVM exit reason, and errno diagnostic.

UART transmit bytes are currently written directly to standard output while
holding the VM big lock. Input is nonblocking and passes through the main
event loop, host-terminal backend, and bounded console queue. Buffered
asynchronous output remains a later performance improvement.

`--test-device` enables the otherwise absent VART-only completion device for
tiny regression guests. It is not part of the Linux machine description.

The option unit test covers defaults, every supported option, size suffixes,
special modes, missing values, invalid sizes, invalid CPU counts, and mixed
modes. The KVM CLI integration test runs the existing MMIO round-trip guest
through the production executable and checks its UART output and exit status.
