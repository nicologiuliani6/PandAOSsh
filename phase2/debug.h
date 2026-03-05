#ifndef DEBUG_H
#define DEBUG_H

/* Debug disabilitato: le chiamate a debug_print/debug_hex
 * generano interrupt TX che interferiscono con l'interrupt handler */
#define debug_print(x)    do {} while(0)
#define debug_hex(x, y)   do {} while(0)

#endif