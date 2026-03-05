#ifndef DEBUG_H
#define DEBUG_H

#define DEBUG_ENABLED false

#if DEBUG_ENABLED

static inline void debug_print(const char *msg) {
    unsigned int *command = (unsigned int *)(0x10000254 + 3*4);
    while (*msg != '\0') {
        *command = 2 | (((unsigned int)*msg) << 8);
        for (volatile int i = 0; i < 10000; i++);
        msg++;
    }
}

static inline void debug_hex(const char *label, unsigned int val) {
    unsigned int *command = (unsigned int *)(0x10000254 + 3*4);
    const char *p = label;
    while (*p) {
        *command = 2 | (((unsigned int)*p) << 8);
        for (volatile int i = 0; i < 10000; i++);
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
        for (volatile int i = 0; i < 10000; i++);
    }
    *command = 2 | (((unsigned int)' ') << 8);
    for (volatile int i = 0; i < 10000; i++);
    *command = 2 | (((unsigned int)'\n') << 8);
    for (volatile int i = 0; i < 10000; i++);
}

#else

#define debug_print(x)  do {} while(0)
#define debug_hex(x, y) do {} while(0)

#endif
#endif