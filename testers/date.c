/* date.c - stampa una data.
 *
 * L'ambiente PandOSsh non dispone di un orologio di sistema (RTC), quindi
 * viene stampata una data fissa di riferimento. */
#include "h/libuser.h"

void main(void) {
    u_print("date: 2024-01-01 00:00:00 (nessun RTC disponibile)\n");
}
