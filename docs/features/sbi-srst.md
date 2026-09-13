# SBI system reset

RISC-V KVM handles the standard SBI System Reset extension in the kernel and
returns `KVM_EXIT_SYSTEM_EVENT` to VART. Shutdown requests map to
`KVM_SYSTEM_EVENT_SHUTDOWN`; cold and warm reboot requests both map to
`KVM_SYSTEM_EVENT_RESET`. The SBI reset reason is carried in system-event data
element zero.

`VartVcpuExit` preserves the complete generic KVM system-event payload: event
type, valid data count, and all 16 data elements. Machine-level policy can
therefore distinguish shutdown, reset, crash, and future events without
accessing `struct kvm_run` directly. VART does not yet perform a machine reset;
the current slice establishes and validates the exit contract first.

## Validation

Three tiny guests request shutdown, cold reboot after system failure, and warm
reboot. The integration test verifies the decoded event type, `ndata`, and SBI
reason. A supported SRST call must not return to the guest.
