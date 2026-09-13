# Tests

All tests and small validation examples live under this tree. Tests are first
classified by execution model and then grouped by subsystem, following the
kernel practice of keeping test code organized separately from production code.

```text
tests/
|-- unit/          host-side tests of isolated C modules
|-- integration/   tests spanning multiple VART subsystems
|-- guests/        tiny RISC-V guest source programs
`-- fixtures/      small static inputs shared by tests
```

Use an additional subsystem directory when a class contains more than one
feature. For example:

```text
tests/unit/address-space/
tests/integration/kvm/
tests/guests/mmio/
tests/fixtures/fdt/
```

Test sources and scripts are committed. Compiled host tests, guest ELF files,
raw guest binaries, logs, and other generated output go under `build/`.

Tests should have one clear purpose, deterministic pass/fail behavior, and a
short comment when their intent is not obvious. A critical feature is not
complete until its relevant tests are wired into `make check`.

