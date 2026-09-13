# rvmm-lab development instructions

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

## Reference implementations

- Consult `../qemu` when KVM behavior, machine layout, device semantics, or an
  error is unclear.
- Consult `../linux` and installed Linux UAPI headers for the KVM ABI. The
  kernel UAPI is authoritative when it differs from QEMU.
- Keep dependencies on sibling source trees out of normal builds. They are
  references and providers of test artifacts, not rvmm-lab source code.
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
- Comments must be written in English. Explain intent, invariants, hardware
  behavior, and non-obvious decisions; do not narrate obvious statements.
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

See `docs/design.md` for the initial subsystem design.

## Testing

- Build and run relevant tests before every commit.
- At minimum, run `make` and `make check` for compiled-code changes.
- Add a focused regression test with each bug fix when practical.
- Test Linux boot milestones on the RISC-V KVM server, not only through QEMU
  TCG.

## Commits

- Make one coherent commit for each working increment.
- Use English commit messages in Linux kernel style:
  `subsystem: imperative summary`.
- Keep the subject concise, normally at most 75 characters, with no trailing
  period.
- Explain why in the body when the reason is not evident from the diff.
- Do not commit generated build artifacts, guest images, or unrelated changes.
- Leave the worktree clean after a completed increment.
