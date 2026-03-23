#ifndef DEBUG_H
#define DEBUG_H


/* mettere 1 per abilitare i print locali di debug
*/
#define DEBUG_INIT 0
#define DEBUG_SCHED 0
#define DEBUG_EXC 0
#define DEBUG_INT 0

/* mettere 0 per disattivare globalmente tutti i print*/
#define DEBUG_ENABLED 1

#if DEBUG_ENABLED
#define DELAY() for (volatile int i = 0; i < 60; i++)
static inline void debug_print(const char *msg) {
    unsigned int *command = (unsigned int *)(0x10000254 + 3*4);
    while (*msg != '\0') {
        *command = 2 | (((unsigned int)*msg) << 8);
        DELAY();
        msg++;
    }
}
static inline void debug_hex(const char *label, unsigned int val) {
    unsigned int *command = (unsigned int *)(0x10000254 + 3*4);
    const char *p = label;
    while (*p) {
        *command = 2 | (((unsigned int)*p) << 8);
        DELAY();
        p++;
    }

    char hex[9];
    for (int i = 7; i >= 0; i--) {
        int nibble = val & 0xF;
        hex[i] = nibble < 10 ? '0' + nibble : 'a' + nibble - 10;
        val >>= 4;
    }

    hex[8] = '\0';

    for (int i = 0; i < 8; i++) {
        *command = 2 | (((unsigned int)hex[i]) << 8);
        DELAY();
    }

    *command = 2 | (((unsigned int)' ') << 8);
    DELAY();
    *command = 2 | (((unsigned int)'\n') << 8);
    DELAY();
}

#else

#define debug_print(msg) ((void)0)
#define debug_hex(label, val) ((void)0)

#endif

#endif