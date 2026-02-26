/*
 * scheduler.c
 *
 * Implementa lo Scheduler del Nucleo: round-robin preemptivo con
 * time slice di 5ms (TIMESLICE).
 *
 * Comportamento:
 *  - Ready Queue non vuota: dispatch del primo processo
 *  - processCount == 0:         HALT (tutti i processi terminati)
 *  - processCount > 0 && softBlockCount > 0: Wait State (attende I/O)
 *  - processCount > 0 && softBlockCount == 0: PANIC (deadlock)
 */

#include "../headers/const.h"
#include "../headers/types.h"
#include <uriscv/liburiscv.h>

#include "../phase1/headers/pcb.h"
#include "./headers/globals.h"

/*
 * scheduler()
 *
 * Seleziona il prossimo processo dalla Ready Queue e lo dispatcha,
 * oppure gestisce il caso in cui la Ready Queue sia vuota.
 */
static void debug_print2(const char *msg) {
    unsigned int *command = (unsigned int *)(0x10000254 + 3*4);
    
    while (*msg != '\0') {
        *command = 2 | (((unsigned int)*msg) << 8);
        /* delay */
        for (volatile int i = 0; i < 10000; i++);
        msg++;
    }
}
// Aggiungi un buffer per stampare indirizzi hex
static void debug_hex(const char *label, unsigned int val) {
    unsigned int *command = (unsigned int *)(0x10000254 + 3*4);
    // stampa label
    const char *p = label;
    while (*p) {
        *command = 2 | (((unsigned int)*p) << 8);
        for (volatile int i = 0; i < 10000; i++);
        p++;
    }
    // stampa val in hex
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
    // newline
    *command = 2 | (((unsigned int)'\n') << 8);
    for (volatile int i = 0; i < 10000; i++);
}
void scheduler(void) {
    debug_print2("\n1");
    /* ------------------------------------------------------------------
     * Caso 1: Ready Queue non vuota → dispatch
     * ------------------------------------------------------------------ */
    if (!emptyProcQ(&readyQueue)) {
        debug_print2(" 1.1");
        /* Rimuove il PCB in testa alla Ready Queue */
        currentProcess = removeProcQ(&readyQueue);
        if (currentProcess == NULL) {
            PANIC(); // non dovrebbe mai accadere
            debug_print2("PANIC");
            }
        debug_print2(" 1.2");

        /* Registra il TOD all'inizio del quanto per calcolare p_time */
        STCK(startTOD);
        debug_print2(" 1.3");

        /* Carica il PLT con il time slice di 5ms */
        setTIMER(TIMESLICE * (*((cpu_t *)TIMESCALEADDR)));
        debug_print2(" 1.4 \n");

        /* Carica lo stato del processo e cede il controllo (non ritorna) */
        debug_hex("pc_epc: ", (unsigned int)currentProcess->p_s.pc_epc);
        debug_hex("reg_sp: ", (unsigned int)currentProcess->p_s.reg_sp);
        debug_hex("status: ", (unsigned int)currentProcess->p_s.status);
        LDST(&(currentProcess->p_s));
        debug_print2(" 1.5  ");

        /* Non si arriva mai qui */
    }
    debug_print2("2");

    /* ------------------------------------------------------------------
     * Ready Queue vuota: controlla processCount e softBlockCount
     * ------------------------------------------------------------------ */

    if (processCount == 0) {
        debug_print2("3");
        /* Nessun processo rimasto: sistema terminato correttamente */
        HALT();
    }
    debug_print2("4");
    if (softBlockCount > 0) {
        debug_print2("5");

        /*
         * Ci sono processi bloccati in attesa di I/O o timer.
         * Entra in Wait State: il processore attende un interrupt.
         *
         * Prima di eseguire WAIT:
         *  - abilita tutti gli interrupt (MIE_ALL)
         *  - disabilita il PLT (toglie MIE_MTIE_MASK) per evitare
         *    che il PLT generi interrupt mentre non c'è un Current Process
         *  - abilita il bit MIE globale nello STATUS
         *
         * currentProcess viene impostato a NULL per segnalare al gestore
         * degli interrupt che non c'è un processo in esecuzione.
         */
        currentProcess = NULL;

        /* Abilita interrupt globali, disabilita PLT */
        setMIE(MIE_ALL & ~MIE_MTIE_MASK);
        unsigned int status = getSTATUS();
        status |= MSTATUS_MIE_MASK;
        setSTATUS(status);
        debug_print2("6");
        /* Attende il prossimo interrupt */
        WAIT();
        debug_print2("7");
        /* Dopo WAIT il controllo passa all'handler: non si torna qui */
    }
    debug_print2("8");
    /* processCount > 0 && softBlockCount == 0: DEADLOCK */
    PANIC();
    debug_print2("9");
}
