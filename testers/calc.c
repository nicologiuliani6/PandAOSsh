/*
 * calc.c - calcolatrice richiesta dalla specifica Phase 3 (Sez. 11).
 *
 * Legge dal terminale una espressione del tipo <cifra><op><cifra>
 * (es. "7*8"), con op in { +, -, *, / } e cifre singole, e stampa il
 * risultato.
 */
#include "h/libuser.h"

/* Converte un intero (anche negativo) in stringa. */
static void itoa(int v, char *out) {
    char tmp[16];
    int  i = 0, j = 0;
    int  neg = 0;

    if (v == 0) { out[0] = '0'; out[1] = '\0'; return; }
    if (v < 0)  { neg = 1; v = -v; }

    while (v > 0) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
    if (neg) out[j++] = '-';
    while (i > 0) out[j++] = tmp[--i];
    out[j] = '\0';
}

void main(void) {
    char buf[128];
    char out[32];

    u_print("calc: inserisci <cifra><op><cifra> (es. 7*8): ");
    int n = u_readline(buf);

    if (n < 3) {
        u_print("calc: input non valido\n");
        return;
    }

    int  a  = buf[0] - '0';
    char op = buf[1];
    int  b  = buf[2] - '0';
    int  r  = 0;
    int  ok = 1;

    switch (op) {
        case '+': r = a + b; break;
        case '-': r = a - b; break;
        case '*': r = a * b; break;
        case '/': if (b == 0) ok = 0; else r = a / b; break;
        default:  ok = 0;
    }

    if (!ok) {
        u_print("calc: operazione non valida\n");
        return;
    }

    u_print("calc: risultato = ");
    itoa(r, out);
    u_print(out);
    u_print("\n");
}
