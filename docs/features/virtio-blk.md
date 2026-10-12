# Virtio block PCI device

Stage 11.5 adds a small virtio-blk PCI function on top of the modern
virtio-pci transport. The device uses one split virtqueue and a raw regular
file backend. Its PCI device owns the DMA address space used for descriptor,
request, and completion accesses. It exposes two MSI-X vectors for the
configuration and single request queue; the virt machine connects them to the
RISC-V AIA.

The initial backend supports 512-byte sector reads, writes, and flushes. The
device configuration reports capacity, maximum segment size, segment count,
and block size. Requests are processed synchronously from the queue notify
callback; asynchronous workers are intentionally deferred until the I/O
context interface is available.

The unit test creates a temporary raw disk, submits a write and a read through
guest RAM, and validates the status byte, used-ring length, and file contents.
The integration path has been validated with a Linux 6.18.3 guest: Linux
enumerates the PCI function, initializes the virtqueue, reads the disk
capacity, and reaches the initramfs shell.
