/*
 * scheduler.c
 */

#include "../headers/const.h"
#include "../headers/types.h"
#include <uriscv/liburiscv.h>

#include "../phase1/headers/pcb.h"
#include "./headers/globals.h"
#include "debug.h"

#define DEBUG_SCHED 1

#if DEBUG_SCHED
#define SDBG(msg)           debug_print(msg)
#define SDBG_HEX(msg,val)   debug_hex(msg,val)
#else
#define SDBG(msg)           ((void)0)
#define SDBG_HEX(msg,val)   ((void)0)
#endif

void scheduler(void) { 

    /* 1. Se c’è un processo ready → eseguilo */
    if (!emptyProcQ(&readyQueue)) {
        currentProcess = removeProcQ(&readyQueue);
        setTIMER(TIMESLICE * (*((cpu_t *) TIMESCALEADDR)));
        LDST(&currentProcess->p_s);
    }

    /* 2. Se non ci sono più processi → HALT */
    if (processCount == 0) {
        SDBG("All processes completed!\n");
        HALT();
    }

    /* 3. Se ci sono processi soft-blocked → WAIT */
    if (softBlockCount > 0) {
        currentProcess = NULL;

        setMIE(MIE_ALL & ~MIE_MTIE_MASK);
        unsigned int status = getSTATUS();
        status |= MSTATUS_MIE_MASK;
        setSTATUS(status);

        WAIT();
    }

    if (emptyProcQ(&readyQueue) && processCount > 0 && softBlockCount == 0) {
        // cerca un processo attivo
        for (int i = 0; i < MAXPROC; i++) {
            if (activeProcs[i] != NULL) {
                currentProcess = activeProcs[i];
                LDST(&currentProcess->p_s);
            }
        }
        // se non trovi nulla → vero deadlock
        SDBG("[DEADLOCK] No ready processes and no soft-blocked processes\n");
        HALT();
    }
    if (processCount == 0) {
        SDBG("System halted\n");
        HALT();
    }
    PANIC(); /* Non dovrebbe mai arrivarci */
}


