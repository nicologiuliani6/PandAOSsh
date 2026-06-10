/* proc_a.c - "shell" di verifica: lancia una U-proc figlia con SYS6. */
#include "h/libuser.h"

void main(void) {
    u_print("A: shell start\n");
    u_execute(2);                 /* lancia la U-proc su flash1 (ASID 2) */
    u_print("A: child terminato, esco\n");
    /* ritorno -> SYS2 */
}
