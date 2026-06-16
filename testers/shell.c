/*
 * shell.c -> shell interattiva richiesta dalla specifica Phase 3 (Sez. 11).
 * è la U-proc lanciata direttamente dall'InstantiatorProcess (ASID 1).
 * Legge un nome di programma dal terminale, lo traduce in un ASID tramite
 * una mappatura statica e lo esegue con la SYS6 (Execute), bloccandosi fino
 * alla terminazione del programma figlio. Il comando "exit" conclude la
 * shell (SYS2), il che porta poi il sistema all'arresto (HALT).
 *
 * Mappatura statica nome -> ASID (ciascun programma ha il proprio flash
 * device precaricato col suo backing store), come da configurazione macchina:
 *   echo -> ASID 2 (flash2)
 *   fibEleven -> ASID 3 (flash3)
 *   uname -> ASID 4 (flash4)
 *   sl -> ASID 5 (flash5)
 *   calc -> ASID 6 (flash6)
 */
#include "h/libuser.h"

static int u_strcmp(const char *a, const char *b) {
    while (*a != '\0' && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

/* Mappatura statica nome programma -> ASID. -1 se sconosciuto. */
static int nameToAsid(const char *name) {
    if (u_strcmp(name, "echo")      == 0) return 2;
    if (u_strcmp(name, "fibEleven") == 0) return 3;
    if (u_strcmp(name, "uname")     == 0) return 4;
    if (u_strcmp(name, "sl")        == 0) return 5;
    if (u_strcmp(name, "calc")      == 0) return 6;
    return -1;
}

void main(void) {
    char buf[128];

    u_print("PandOSsh shell\n");
    u_print("comandi: echo fibEleven uname sl calc | exit\n");

    while (1) {
        u_print("> ");

        int n = u_readline(buf);

        /* Rimuove il newline finale e termina la stringa. */
        if (n > 0 && buf[n - 1] == '\n')
            buf[n - 1] = '\0';
        else
            buf[n] = '\0';

        if (buf[0] == '\0')
            continue;                 /* riga vuota: nuovo prompt */

        if (u_strcmp(buf, "exit") == 0) {
            u_print("shell: arrivederci\n");
            u_terminate();            /* SYS2: non ritorna */
        }

        int asid = nameToAsid(buf);
        if (asid < 0) {
            u_print("shell: programma sconosciuto\n");
            continue;
        }

        /* SYS6: lancia il programma e attende la sua terminazione. */
        u_execute(asid);
    }
}
