#include "../headers/const.h"
#include "../headers/types.h"
#include <uriscv/liburiscv.h>

#include "../phase1/headers/pcb.h"
#include "./headers/globals.h"
#include "debug.h"

void scheduler(void) {

    /* 1. Se c’è un processo pronto, eseguilo */
    if (!emptyProcQ(&readyQueue)) {
        currentProcess = removeProcQ(&readyQueue);
        if (currentProcess == NULL) PANIC();

        STCK(startTOD);

        cpu_t plt = TIMESLICE * (*((cpu_t *)TIMESCALEADDR));
        setTIMER(plt);

        LDST(&(currentProcess->p_s));
    }

    /* 2. Nessun processo pronto */
    if (processCount == 0) {
        HALT();
    }

    /* 3. Ci sono processi vivi e almeno uno soft‑bloccato → WAIT */
    if (processCount > 0 && softBlockCount > 0) {
        currentProcess = NULL;

        setMIE(MIE_ALL & ~MIE_MTIE_MASK);
        unsigned int status = getSTATUS();
        status |= MSTATUS_MIE_MASK;
        setSTATUS(status);

        WAIT();
        return;
    }

    /* 4. Deadlock: processi vivi ma nessuno pronto e nessuno soft‑bloccato */
    PANIC();
}
