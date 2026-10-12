# Virtio block PCI device

Stage 11.5 adds a small virtio-blk PCI function on top of the modern
virtio-pci transport. The device uses one split virtqueue and a raw regular
file backend. Its PCI device owns the DMA address space used for descriptor,
request, and completion accesses.

The initial backend supports 512-byte sector reads, writes, and flushes. The
device configuration reports capacity, maximum segment size, segment count,
and block size. Requests are processed synchronously from the queue notify
callback; asynchronous workers are intentionally deferred until the I/O
context interface is available.

The unit test creates a temporary raw disk, submits a write and a read through
guest RAM, and validates the status byte, used-ring length, and file contents.
It does not claim Linux boot support yet; the next increment must connect this
function to the virt machine and validate enumeration and root-disk access.
