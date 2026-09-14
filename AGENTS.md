# VART development instructions

These instructions apply to the entire repository.

## Project scope

- Build a small RV64 virtual machine monitor that uses Linux KVM.
- The first milestone is a single-vCPU Linux guest booting to an interactive
  initramfs shell.
- Model QEMU's RISC-V `virt` machine where this helps Linux compatibility, but
  do not reproduce infrastructure that this project does not need.
- Use RISC-V AIA for guest interrupts: APLIC for wired interrupt sources and
  IMSIC for message-signaled interrupts.
- Prefer a simple, correct implementation first. Keep subsystem boundaries
  suitable for later asynchronous and high-performance I/O.
- Split a roadmap stage into smaller reviewable increments whenever its design,
  implementation, or test surface becomes too large for timely audit. Each
  increment must have a narrow purpose and leave the tree in a working state;
  stage boundaries are planning aids, not minimum commit sizes.

## Reference implementations

- Consult `../qemu` when KVM behavior, machine layout, device semantics, or an
  error is unclear.
- Consult `../linux` and installed Linux UAPI headers for the KVM ABI. The
  kernel UAPI is authoritative when it differs from QEMU.
- Keep dependencies on sibling source trees out of normal builds. They are
  references and providers of test artifacts, not VART source code.
- Record the upstream file and revision when code is derived from QEMU.
  Preserve its copyright and SPDX notices. Do not copy code whose license is
  incompatible with the repository license.

## C style

- Write C in the style used by QEMU and the Linux kernel where practical.
- Use four spaces for C indentation and tabs only where Make syntax requires
  them.
- Keep lines near 80 columns and avoid trailing whitespace.
- Use braces around every control-flow body, including one-line bodies.
- Use `lower_case_with_underscores` for functions and variables. Give public
  subsystem functions a consistent prefix.
- Keep declarations close to first use when that improves clarity.
- Comments must be written in English and are required for core logic whose
  intent is not clear from the code alone. In particular, document subtle KVM
  contracts, guest-visible hardware behavior, concurrency and lifetime
  invariants, ordering requirements, and non-obvious error handling.
- Use function comments when a public or complex function has parameters,
  ownership, locking, address-space context, side effects, or return conventions
  that its declaration does not make clear. Describe only the non-obvious parts.
- Explain why a design or operation is necessary rather than translating each
  statement into prose. Do not comment simple assignments, straightforward
  wrappers, or control flow that is already self-explanatory.
- Keep comments close to the code they constrain and update them with the code.
  Stale or speculative comments are bugs. Follow the restraint and tone used in
  Linux and QEMU instead of trying to maximize comment density.
- Treat warnings as errors in normal development builds.

## Architecture rules

- Keep KVM lifecycle, vCPU state, guest memory, interrupt routing, bus models,
  device models, and the event loop in separate modules.
- Treat the guest physical address space as a container for independently
  registered regions. Do not equate the address space, platform bus, MMIO
  dispatcher, and PCIe bus.
- Design bus and device interfaces for both platform devices and a modern PCIe
  hierarchy. PCIe support must be able to add a root complex, ECAM
  configuration space, bridges, BAR mapping, INTx, MSI/MSI-X, DMA, and device
  lifecycle without changing the core MMIO API.
- Every DMA-capable device must use an attached DMA address space. Never let a
  device access guest RAM by treating a DMA address as a guest physical address.
  The attachment must allow an emulated IOMMU to replace or interpose on the
  device's translation path without changing the device model.
- Device models must interact through explicit MMIO, IRQ, timer, and DMA
  interfaces. They must not depend directly on the current polling loop.
- Do not expose GLib, coroutine, or a particular host AIO backend in device
  model interfaces. A future event backend must be replaceable without
  rewriting devices.
- Start with a single-threaded blocking or polling event loop. Preserve device
  state ownership and callback boundaries needed for a later epoll/io_uring,
  worker-thread, or coroutine implementation.
- Avoid global mutable state. Pass VM, vCPU, bus, and device objects explicitly.
- Check guest-provided addresses, lengths, and descriptor data before use.
- Support multiple vCPUs as a fundamental requirement. Begin with a VM-wide
  big lock for guest-visible state, then add narrowly scoped locks only with a
  documented ownership rule, lock order, and measured reason.
- Do not hold device or subsystem locks while invoking callbacks into another
  subsystem unless the lock order explicitly permits it.

See `docs/design.md` for the initial subsystem design.
Follow `docs/roadmap.md` for development order and milestone exit criteria.

## Testing

- Build and run relevant tests before every commit.
- At minimum, run `make` and `make check` for compiled-code changes.
- Add a focused regression test with each bug fix when practical.
- Before Linux can boot, validate each critical subsystem with the smallest
  practical bare-metal guest, host-side unit test, or integration test.
- For a critical feature, cover its normal path, important boundary values,
  invalid inputs, reset behavior, and failure cleanup where applicable.
- Prefer deterministic tests. Do not rely only on manual console inspection.
- Keep every test and test example under `tests/`, classified as `unit/`,
  `integration/`, `guests/`, or `fixtures/`, and then by subsystem. Do not put
  ad-hoc test programs in the repository root or production source tree.
- Keep tiny bare-metal guest sources in `tests/guests/<subsystem>/`. Generated
  guest ELF and binary files belong under `build/` and must not be committed.
- Test Linux boot milestones on the RISC-V KVM server, not only through QEMU
  TCG.

## Documentation and debugging records

- Keep `docs/design.md` as a concise overview of the current architecture.
- Give each independently useful subsystem or feature a focused document under
  `docs/features/`. Describe its purpose, interfaces, important invariants,
  guest-visible behavior, and test strategy without duplicating source code.
- Update architecture and feature documentation in the same commit as a change
  that makes the existing description inaccurate.
- Record difficult, surprising, or time-consuming bugs under
  `docs/debugging/`. Include symptoms, reproduction, investigation, root cause,
  fix, and regression coverage.
- Do not create debugging records for routine compiler errors or obvious typos.
  The purpose is to retain reusable diagnostic knowledge for review.
- Write documentation and debugging records in English to keep the repository
  consistent with code comments and commit messages.

## Commits

- Make one coherent commit for each working increment.
- Keep commits small enough for line-by-line review. Separate foundational
  interfaces, implementation, tests, and later refinements when each part can
  compile and be validated independently.
- Use English commit messages in Linux kernel style:
  `subsystem: imperative summary`.
- Keep the subject concise, normally at most 75 characters, with no trailing
  period.
- Explain why in the body when the reason is not evident from the diff.
- Do not commit generated build artifacts, guest images, or unrelated changes.
- Leave the worktree clean after a completed increment.

## Remote repository

- Access the GitHub remote through SSH on `ssh.github.com` port 443 because
  the direct GitHub route from the RISC-V development server is unreliable.
- The server SSH client uses the reverse proxy tunnel at `127.0.0.1:17897`.
  Every Windows SSH connection that may fetch or push must explicitly add
  `-o ExitOnForwardFailure=yes` and
  `-R 127.0.0.1:17897:127.0.0.1:7897`. The forward uses the Windows proxy at
  `127.0.0.1:7897` and lasts only for that server connection. Do not bypass the
  configured `ProxyCommand` with an ad-hoc `GIT_SSH_COMMAND`.
- Keep `origin` set to
  `ssh://git@ssh.github.com:443/sasasashui/vart.git` for both fetch and push.
- Do not replace this repository-specific URL with an HTTPS remote unless the
  server network policy changes and connectivity is verified.
- See `docs/development/github-ssh-proxy.md` for the connection topology,
  verification, and recovery procedure.
