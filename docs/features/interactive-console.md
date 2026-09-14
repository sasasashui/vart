# Interactive console

The production VART runtime connects standard input to the NS16550A through
the host terminal, event loop, and bounded console queue. The main thread owns
input readiness and performs nonblocking reads. It takes the VM big lock only
while queueing bytes and advancing UART state; host reads never hold the lock.

Input readiness remains disabled until the guest driver enables UART receive
interrupts, preventing early serial initialization from discarding piped
input. The console also disables readiness when its ring is full. A vCPU that
enables receive interrupts or frees queue space while servicing MMIO wakes the
main loop, which re-enables readiness. Guest shutdown wakes the loop. A
100-millisecond health interval detects a vCPU that stops because `KVM_RUN`
failed before an ordinary exit handler could issue a wakeup.

TTY input runs in raw mode and is restored during normal cleanup. Pipes and
redirected files use the same nonblocking path and disable their event source
after EOF. UART output remains synchronous for now; a buffered asynchronous
output path can be added without changing the console input contract.

The pseudo-terminal integration test gives VART a controlling PTY and boots
Linux to its initramfs prompt. It verifies raw mode while the guest runs, sends
a shell command through the UART receive path, observes its output and clean
poweroff, and then compares every saved `termios` field and file status flag.
A second boot writes the terminal's interrupt character and requires VART to
handle the generated `SIGINT`, stop its KVM vCPU, exit with status 130, and
restore the same terminal state.
