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

`VartAddressSpace` owns a replaceable array of region references. It permits
overlays with distinct priorities and rejects ambiguous equal-priority overlap.
Lookup returns the highest-priority enabled region containing the complete
access. Registration, removal, and enabled changes are explicit topology
mutation boundaries where listeners will be added later.

The address space does not own region lifetime. Destroying it detaches all
regions. A region can belong to only one address space at a time, and direct
enabled changes are rejected while it is registered.

MMIO regions may carry read and write callbacks plus an opaque device pointer.
Address-space access validates 1, 2, 4, or 8-byte operations, translates the
guest address to a region-relative offset, masks values to the access width,
and propagates device errors. Missing mappings, non-MMIO mappings, and absent
callbacks have distinct errors.

No topology listeners, aliases, or RAM dispatch is implemented yet. The current
array favors simple auditable behavior and can be replaced without changing the
public interface.

## Validation

`tests/unit/address-space/region.c` covers invalid types, zero size, overflow,
first and last bytes, below-range and crossing accesses, zero-length boundary
semantics, metadata, owner identity, and disabled regions.

`tests/unit/address-space/topology.c` covers adjacent regions, ambiguous and
prioritized overlap, duplicate registration, lookup precedence, enable changes,
removal, and detach-on-destroy behavior.

`tests/unit/address-space/mmio.c` covers read and write offsets, all validation
paths, width masking, region boundaries, callback errors, non-MMIO regions, and
read-only devices.
