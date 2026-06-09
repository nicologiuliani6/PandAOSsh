/*
 * scheduler.c
 */

#include "../headers/const.h"
#include "../headers/types.h"
#include <uriscv/liburiscv.h>

#include "../phase1/headers/pcb.h"
#include "./headers/globals.h"
#include "debug.h"


#if DEBUG_SCHED
#define SDBG(msg)           debug_print(msg)
#define SDBG_HEX(msg,val)   debug_hex(msg,val)
#else
#define SDBG(msg)           ((void)0)
#define SDBG_HEX(msg,val)   ((void)0)
#endif

void scheduler(void) {

    /* 0. Gestione di un eventuale processo che ha appena fatto YIELD:
     * se esiste un processo pronto di priorità >= a quella dello yielder,
     * lo yielder viene rimesso in coda (cede la CPU); altrimenti riprende
     * subito perché è comunque il più prioritario. */
    if (yieldedProcess != NULL) {
        pcb_t *y = yieldedProcess;
        yieldedProcess = NULL;
        if (!emptyProcQ(&readyQueue) &&
            headProcQ(&readyQueue)->p_prio >= y->p_prio) {
            insertProcQ(&readyQueue, y);
        } else {
            currentProcess = y;
            STCK(startTOD);
            setTIMER(TIMESLICE * (*((cpu_t *) TIMESCALEADDR)));
            LDST(&currentProcess->p_s);
        }
    }

    /* 1. Se c’è un processo ready lo eseguiamo (round-robin con time slice) */
    if (!emptyProcQ(&readyQueue)) {
        currentProcess = removeProcQ(&readyQueue);
        STCK(startTOD);
        setTIMER(TIMESLICE * (*((cpu_t *) TIMESCALEADDR)));
        LDST(&currentProcess->p_s);
    }

    /* Nessun processo in esecuzione da qui in poi */
    currentProcess = NULL;

    /* 2. Nessun processo vivo: HALT del sistema */
    if (processCount == 0) {
        SDBG("All processes completed!\n");
        HALT();
    }

    /* 3. Processi vivi ma tutti soft-blocked: attesa di un interrupt (WAIT) */
    if (softBlockCount > 0) {
        setMIE(MIE_ALL & ~MIE_MTIE_MASK);
        unsigned int status = getSTATUS();
        status |= MSTATUS_MIE_MASK;
        setSTATUS(status);

        WAIT();
    }

    /* 4. Processi vivi, nessuno pronto e nessuno soft-blocked: DEADLOCK */
    SDBG("[DEADLOCK] No ready processes and no soft-blocked processes\n");
    PANIC();
}


