# Address regions

## Purpose

`VartAddressRegion` describes a typed range that can later be published into an
address space. It separates guest address topology from KVM memory slots and
from any particular bus.

## Design

A region records its inclusive start through size, type, lookup priority,
enabled state, and owning object. Initialization rejects empty, wrapping, and
unknown region definitions. Range checks avoid end-address arithmetic so a
guest-controlled address cannot wrap around `UINT64_MAX`.

A zero-length query is valid at any position through the byte immediately after
the region. Nonempty queries must remain wholly inside the region. Disabled
regions match no queries.

## Limitations

No address-space container, overlap resolution, MMIO operations, aliases, or
RAM association is implemented yet. Type and priority are metadata for the next
increment.

## Validation

`tests/unit/address-space/region.c` covers invalid types, zero size, overflow,
first and last bytes, below-range and crossing accesses, zero-length boundary
semantics, metadata, owner identity, and disabled regions.
