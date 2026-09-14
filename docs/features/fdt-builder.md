# Flattened device tree builder

## Purpose

VART builds the small RISC-V machine description without taking a runtime
dependency on libfdt. The builder emits the standard flattened device tree
format consumed by Linux while keeping machine-specific nodes outside the
format layer.

## Interface and ownership

`vart_fdt_init()` allocates separate, caller-sized structure and string
buffers. Callers append nodes and properties in depth-first order, close the
root, and call `vart_fdt_finish()`. The returned DTB is owned by the caller;
the builder retains its working buffers until `vart_fdt_destroy()`.

Property helpers encode strings, individual 32-bit cells, cell arrays, and
64-bit two-cell values.
The generic byte-property interface supports empty properties and arbitrary
future bindings. Property names are stored once in the strings block and
reused by offset.

## Invariants and errors

The builder enforces a single empty-name root, balanced node nesting,
properties before child nodes, node names without path separators, and no
mutation after finalization. Every structure token and property value is
four-byte aligned; the memory-reservation map begins on an eight-byte boundary.
Header fields and cells are emitted in big-endian order.

The caller selects hard capacities so an unexpectedly large machine tree fails
with `-ENOSPC` instead of silently reallocating or producing a partial blob.
Size arithmetic and 32-bit format limits are checked separately. Failed
operations do not append a partial structure token.

## QEMU relationship

QEMU's `hw/riscv/virt.c` constructs its machine tree through libfdt helpers and
adds machine nodes independently of the binary-format implementation. VART
keeps the same separation: later Stage 6 increments will reproduce the required
`virt` bindings using this builder rather than embedding node policy here.

## Validation

The unit test builds a root with scalar, string, empty, nested, and duplicate
property-name cases. It independently walks the resulting tokens and verifies
the header, offsets, sizes, alignment, endian conversion, string reuse, and
error paths. It writes the generated blob under `build/tests/` so an external
`dtc` can parse and decompile the same artifact.
