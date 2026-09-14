#ifndef VART_TERMINAL_H
#define VART_TERMINAL_H

#include <stdbool.h>
#include <termios.h>

typedef struct VartTerminal {
    struct termios original_termios;
    int fd;
    int original_flags;
    bool is_tty;
    bool active;
} VartTerminal;

int vart_terminal_init(VartTerminal *terminal, int fd);
int vart_terminal_restore(VartTerminal *terminal);

#endif
