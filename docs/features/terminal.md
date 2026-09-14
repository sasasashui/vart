# Host terminal

`VartTerminal` prepares an input file descriptor for event-driven console
use. It records the existing file status flags, enables `O_NONBLOCK`, and, for
a TTY, saves its `termios` state and applies raw mode. Non-TTY inputs such as
pipes and redirected files receive only nonblocking mode.

The terminal does not own or close the file descriptor. Its owner calls
`vart_terminal_restore()` before closing it. Restore reinstates both the saved
TTY attributes and the exact original file status flags and is idempotent
after success. A restore failure leaves the object active so cleanup code can
report or retry it.

This module contains no UART, event-loop, or VM policy. Stage 9.5 will connect
it to the console frontend, while signal-safe coordinated restoration remains
part of Stage 9.6.
