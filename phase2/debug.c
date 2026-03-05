#include "debug.h"

static inline void delay(void) {
    for (volatile int i = 0; i < 10000; i++);
}

void debug_print(const char *msg) {
    unsigned int *command = (unsigned int *)(0x10000254 + 3*4);
    
    while (*msg != '\0') {
        *command = 2 | (((unsigned int)*msg) << 8);
        delay();
        msg++;
    }
}

void debug_hex(const char *label, unsigned int val) {
    unsigned int *command = (unsigned int *)(0x10000254 + 3*4);

    /* print label */
    const char *p = label;
    while (*p) {
        *command = 2 | (((unsigned int)*p) << 8);
        delay();
        p++;
    }

    /* convert value to hex string */
    char hex[9];
    for (int i = 7; i >= 0; i--) {
        int nibble = val & 0xF;
        hex[i] = nibble < 10 ? '0' + nibble : 'a' + nibble - 10;
        val >>= 4;
    }
    hex[8] = '\0';

    /* print hex string */
    for (int i = 0; i < 8; i++) {
        *command = 2 | (((unsigned int)hex[i]) << 8);
        delay();
    }

    /* add a space and newline per leggibilità */
    *command = 2 | (((unsigned int)' ') << 8);
    delay();
    *command = 2 | (((unsigned int)'\n') << 8);
    delay();
}
