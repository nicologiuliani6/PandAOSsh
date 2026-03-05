#include "debug.h"

/*
 * debug_print / debug_hex - scrittura diretta al terminale 0 in polling.
 *
 * Meccanismo identico a termprint() in p1test.c.
 * NON usa liburiscv, NON tocca registri MIE/STATUS.
 * Funziona in qualsiasi contesto (boot, kernel, processo).
 *
 * Terminale 0:
 *   TRANSM_STATUS  = 0x1000025C  (offset 8 da base 0x10000254)
 *   TRANSM_COMMAND = 0x10000260  (offset 12 da base)
 *
 * Valori status: READY=1, BUSY=3, TRANSMITTED=5
 */

#define TERM0_TRANSM_STATUS  ((volatile unsigned int *)0x1000025C)
#define TERM0_TRANSM_COMMAND ((volatile unsigned int *)0x10000260)
#define STATUSMASK  0xFF
#define READY       1
#define BUSY        3
#define TRANSMITTED 5
#define PRINTCHR    2

static void write_char(char c) {
    volatile unsigned int *statusp  = TERM0_TRANSM_STATUS;
    volatile unsigned int *commandp = TERM0_TRANSM_COMMAND;

    unsigned int stat = *statusp & STATUSMASK;

    /* Aspetta che il device sia disponibile */
    while (stat != READY && stat != TRANSMITTED) {
        stat = *statusp & STATUSMASK;
    }

    /* Invia il carattere */
    *commandp = PRINTCHR | (((unsigned int)(unsigned char)c) << 8);

    /* Aspetta completamento */
    stat = *statusp & STATUSMASK;
    while (stat == BUSY) {
        stat = *statusp & STATUSMASK;
    }
}

void debug_print(const char *msg) {
    while (*msg != '\0') {
        write_char(*msg);
        msg++;
    }
}

void debug_hex(const char *label, unsigned int val) {
    const char *p = label;
    while (*p) {
        write_char(*p);
        p++;
    }

    char hex[9];
    for (int i = 7; i >= 0; i--) {
        int nibble = val & 0xF;
        hex[i] = nibble < 10 ? '0' + nibble : 'a' + nibble - 10;
        val >>= 4;
    }
    hex[8] = '\0';

    for (int i = 0; i < 8; i++)
        write_char(hex[i]);

    write_char(' ');
    write_char('\n');
}