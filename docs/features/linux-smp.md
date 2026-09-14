# Linux SMP boot

`make check-linux-7.3-rc2-smp` boots the lab's Linux 7.3-rc2 image with two
vCPUs. Hart 0 enters the kernel directly in supervisor mode while hart 1 starts
in the KVM stopped state. Linux uses the KVM-provided SBI HSM implementation to
start hart 1 at its secondary entry point.

The generated device tree describes both CPUs and one supervisor IMSIC file
per hart. The integration test requires the Linux 7.3.0-rc2 banner, the kernel
message reporting two online CPUs, APLIC and ttyS0 initialization, and exactly
two processors in `/proc/cpuinfo`. It then exercises the initramfs shell over
the UART interrupt path and shuts the SMP guest down through SBI.

The same Linux test executable accepts optional run-count and vCPU-count
arguments. This keeps the uniprocessor, repeated-cleanup, and SMP paths on the
same boot implementation while allowing each Make target to state its own
acceptance contract.
