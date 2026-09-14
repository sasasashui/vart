# Closed standard input aliases the KVM descriptor

## Context

The problem was found while adding Stage 9.7 runtime cleanup tests after commit
`b39404f`. VART was running on the RISC-V KVM server with a tiny UART guest.

## Symptoms

Starting VART with standard input closed unexpectedly succeeded in terminal
initialization. The intended failure-path test instead ran the guest normally.
This was dangerous because the console input source appeared valid even though
the process had no input stream.

## Reproduction

Run a guest after closing file descriptor zero:

```text
build/vart --kernel build/guests/cpu/mmio-roundtrip.bin \
    --memory 64M --test-device <&-
```

Before the fix, inspect the process descriptors while it is running. The KVM
descriptor occupies fd 0 rather than a terminal or `/dev/null` descriptor.

## Investigation

The terminal initializer correctly rejected a genuinely invalid high-numbered
fd. It did not reject fd 0 in the process test. Tracing resource acquisition
order showed that VART opened `/dev/kvm` before initializing the terminal.
Linux always returns the lowest available descriptor, so the KVM open filled
the hole left by the closed standard input. Testing only `fcntl(fd, F_GETFL)`
could not distinguish that valid KVM descriptor from an input descriptor.

This is a reusable diagnostic rule: when a supposedly closed well-known fd is
valid later in startup, inspect earlier opens before blaming validation code.

## Root cause

The entry point assumed descriptors 0, 1, and 2 were always open. POSIX process
launch does not guarantee that invariant. Later subsystems also identified
resources by conventional standard-fd numbers, allowing an unrelated resource
to alias the missing descriptor.

## Fix

`vart_runtime_prepare_standard_fds()` runs before command-line processing and
before opening KVM. It opens each missing standard descriptor on `/dev/null`,
using read access for stdin and write access for stdout and stderr. Subsequent
resource opens can no longer reuse those numbers.

## Regression coverage

`tests/unit/runtime/cleanup.c` closes fd 0 in a child, prepares the standard
descriptors, and verifies through `/proc/self/fd/0` that it refers to
`/dev/null` with read-only access. `tests/integration/runtime/error-cleanup.sh`
also launches VART with stdin closed and requires the tiny UART guest to finish
successfully using the intended `/dev/null` EOF behavior.
