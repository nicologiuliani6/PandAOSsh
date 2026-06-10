#ifndef LIBUSER_H
#define LIBUSER_H

/* Mini libreria per le U-proc di test del Support Level (Phase 3). */

#include <uriscv/liburiscv.h>

#define TERMINATE      2
#define WRITETERMINAL  4
#define READTERMINAL   5
#define EXECUTE        6

static int u_strlen(const char *s) {
    int n = 0;
    while (s[n] != '\0') n++;
    return n;
}

/* Scrive una stringa sul terminale (SYS4). */
static void u_print(const char *s) {
    SYSCALL(WRITETERMINAL, (unsigned int)s, u_strlen(s), 0);
}

/* Legge una riga dal terminale nel buffer (SYS5). Ritorna i caratteri letti. */
static int u_readline(char *buf) {
    return (int)SYSCALL(READTERMINAL, (unsigned int)buf, 0, 0);
}

/* Lancia la U-proc con dato ASID (SYS6) e attende la sua terminazione. */
static int u_execute(int asid) {
    return (int)SYSCALL(EXECUTE, (unsigned int)asid, 0, 0);
}

/* Termina la U-proc corrente (SYS2). */
static void u_terminate(void) {
    SYSCALL(TERMINATE, 0, 0, 0);
}

#endif /* LIBUSER_H */
