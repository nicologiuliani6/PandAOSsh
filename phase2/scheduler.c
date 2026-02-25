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
void scheduler(void) {

    /* ------------------------------------------------------------------
     * Caso 1: Ready Queue non vuota → dispatch
     * ------------------------------------------------------------------ */
    if (!emptyProcQ(&readyQueue)) {

        /* Rimuove il PCB in testa alla Ready Queue */
        currentProcess = removeProcQ(&readyQueue);

        /* Registra il TOD all'inizio del quanto per calcolare p_time */
        STCK(startTOD);

        /* Carica il PLT con il time slice di 5ms */
        setTIMER(TIMESLICE * (*((cpu_t *)TIMESCALEADDR)));

        /* Carica lo stato del processo e cede il controllo (non ritorna) */
        LDST(&(currentProcess->p_s));

        /* Non si arriva mai qui */
    }

    /* ------------------------------------------------------------------
     * Ready Queue vuota: controlla processCount e softBlockCount
     * ------------------------------------------------------------------ */

    if (processCount == 0) {
        /* Nessun processo rimasto: sistema terminato correttamente */
        HALT();
    }

    if (softBlockCount > 0) {
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

        /* Attende il prossimo interrupt */
        WAIT();

        /* Dopo WAIT il controllo passa all'handler: non si torna qui */
    }

    /* processCount > 0 && softBlockCount == 0: DEADLOCK */
    PANIC();
}
