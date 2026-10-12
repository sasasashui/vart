# virtio-net TX must accept a packet in one descriptor

## Symptom

The first Linux connectivity test reached the virtio-net device but VART
stopped the guest with `Protocol error` when the guest transmitted its ARP
request. Host-side virtio-net unit tests passed because they constructed a
chain with a separate 12-byte header descriptor and packet descriptor.

## Reproduction

Boot Linux with an inherited socketpair backend and configure the guest
interface with a static address. The following unprivileged test exercises the
path without TAP or host network changes:

```sh
make check-linux-6.18.3-virtio-net
```

The original implementation failed on the first guest-generated packet.

## Root cause

The virtio-net TX implementation required at least two descriptors and treated
the first descriptor as a header-only buffer. The virtio specification permits
the header and packet data to share one descriptor, and Linux uses this layout
for some packets. VART consequently returned `-EPROTO` before reading the
packet from guest memory.

## Fix

The TX path now consumes payload bytes after the 12-byte header in the first
descriptor and appends payload bytes from any following descriptors. It still
rejects writable TX descriptors, malformed chains, and arithmetic overflow.

## Regression coverage

The virtio-net unit test covers both a header-plus-payload chain and a single
descriptor containing the complete header and packet. The Linux integration
test handles ARP and ICMP replies through the socketpair backend and requires
successful guest ping on Linux 6.18.3 and 7.3-rc2.
