# Userspace SBI dispatch

RISC-V KVM forwards legacy, experimental, vendor, and selected optional SBI
extensions through `KVM_EXIT_RISCV_SBI`. VART decodes the extension ID,
function ID, and six argument registers into `VartVcpuExit`, then writes the
SBI error and value pair back before the vCPU resumes.

`VartRiscvSbiDispatcher` maintains sorted, non-overlapping extension ranges.
Callers own handler storage and may register machine-specific implementations
without coupling them to the vCPU run loop. An unregistered extension receives
`SBI_ERR_NOT_SUPPORTED`; callback failures remain host errors and do not resume
the guest with a fabricated result.

Handlers are registered while the machine is being built. Dispatch runs from
the vCPU exit handler under the VM big lock, so the initial implementation
needs no separate dispatcher lock. Dynamic registration is not supported.

This framework does not duplicate standard extensions already implemented by
KVM. A future DBCN implementation can register its extension when VART gains a
console backend.

## Validation

The tiny guest first calls an unregistered experimental extension and checks
the default error. It then calls a registered vendor extension with six known
arguments and checks the userspace-provided result. The host also rejects an
overlapping registration and a second completion of the same exit.
