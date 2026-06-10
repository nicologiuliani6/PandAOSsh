/* proc_b.c - U-proc figlia lanciata via SYS6 da proc_a. */
#include "h/libuser.h"

void main(void) {
    u_print("B: ciao dal figlio (ASID 2)\n");
    /* ritorno -> SYS2 -> sblocca la shell */
}
