# Test device

The VART-only test device gives tiny guests deterministic output, scratch
storage, completion status, and a version register without depending on UART,
interrupts, SBI, or Linux.

| Offset | Width | Access | Meaning |
|---:|---:|:---:|---|
| `0x00` | 1 | W | Emit one byte through the host output callback |
| `0x08` | 8 | RW | Scratch register |
| `0x10` | 4 | RW | 0 none, 1 pass, 2 fail |
| `0x18` | 4 | R | Device ABI version, currently 1 |

Unsupported offsets, widths, directions, and status values return `-EINVAL`.
Reset clears scratch and status but preserves the configured output endpoint.
The device is testing infrastructure and is not part of the Linux machine.
