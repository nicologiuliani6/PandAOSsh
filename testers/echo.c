/* echo.c - legge una riga dal terminale e la ristampa. */
#include "h/libuser.h"

void main(void) {
    char buf[128];

    u_print("echo> ");
    int n = u_readline(buf);     /* SYS5: include il newline finale */
    if (n < 0) return;

    SYSCALL(WRITETERMINAL, (unsigned int)buf, n, 0);  /* SYS4: ristampa */
}
