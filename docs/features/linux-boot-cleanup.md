# Linux boot cleanup

`make check-linux-6.18.3-repeat` performs five complete Linux boots in one VART
process while keeping only the top-level `/dev/kvm` descriptor open. Every
iteration creates a new VM, RAM mapping, vCPU, in-kernel AIA device, address
space, and UART. The guest reaches the initramfs shell and powers off through
SBI before the machine is joined and destroyed.

After every destruction the test checks that the process has the same number
of open file descriptors as before creation. It also verifies that the public
machine state was reset, including the VM and AIA descriptor sentinels and the
vCPU allocation. The following iteration then exercises a fresh KVM object
graph, catching cleanup defects that a one-process-per-boot test would hide.

Before the boot loop, the test also loads the real kernel followed by a
deliberately missing initramfs. It requires the expected `ENOENT` result and
applies the same descriptor and reset-state checks after destroying this
partially prepared machine.

This test covers deterministic userspace cleanup after normal guest shutdown
and boot-file preparation failure. Forced termination and host-signal cleanup
belong to the event-loop and process-lifecycle work in Stage 9.
