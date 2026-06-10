/*
 * test0.c - U-proc minima di verifica del Support Level (Phase 3).
 *
 * Esercita: avvio U-proc, paging (caricamento .text/.rodata dal flash),
 * SYS4 (WriteTerminal) e terminazione automatica (SYS2 via crtsi).
 */

#include <uriscv/liburiscv.h>

#define WRITETERMINAL 4
#define READTERMINAL  5
#define TERMINATE     2

static int slen(const char *s) {
    int n = 0;
    while (s[n] != '\0') n++;
    return n;
}

void main(void) {
    char *msg = "Phase3 OK: U-proc avviata, paging e SYS4 funzionano!\n";
    SYSCALL(WRITETERMINAL, (unsigned int)msg, slen(msg), 0);
    /* ritorno da main -> crtsi esegue SYS2 (TERMINATE) */
}
