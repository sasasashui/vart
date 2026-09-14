# Linux initramfs userspace

VART boots Linux 6.18.3 with the lab initramfs and reaches its `/init` script.
The script mounts procfs, sysfs, and devtmpfs, prints the environment banner,
reports the kernel version, and replaces itself with BusyBox `sh`. The shell
therefore remains PID 1 and presents its `~ # ` prompt on ttyS0.

`make check-linux-6.18.3-userspace` validates this path through observable
guest behavior. It sends commands through the emulated UART receive interrupt,
checks that the shell is PID 1, verifies all three filesystems in
`/proc/mounts`, and executes a shell marker before requesting SBI shutdown.
Markers are assembled by `printf` so terminal echo cannot be mistaken for
successful command execution.

The test uses a fixed initramfs supplied by the surrounding lab. Creating and
maintaining VART-owned guest images remains separate from the virtual-machine
boot path.
