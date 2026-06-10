/* fibEight.c - calcola e stampa l'ottavo numero di Fibonacci. */
#include "h/libuser.h"

static int fib(int n) {
    int a = 0, b = 1;
    for (int i = 0; i < n; i++) {
        int t = a + b;
        a = b;
        b = t;
    }
    return a;
}

void main(void) {
    u_print("fib(8) = ");
    u_printint(fib(8));
    u_print("\n");
}
