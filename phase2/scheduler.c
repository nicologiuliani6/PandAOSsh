/*
 * scheduler.c
 */

#include "../headers/const.h"
#include "../headers/types.h"
#include <uriscv/liburiscv.h>

#include "../phase1/headers/pcb.h"
#include "./headers/globals.h"
#include "debug.h"

void scheduler(void) {
    /* READY QUEUE NON VUOTA */
    if (!emptyProcQ(&readyQueue)) {

        currentProcess = removeProcQ(&readyQueue);
        if (!currentProcess) HALT();

        setTIMER(TIMESLICE * (*((cpu_t *) TIMESCALEADDR)));

        LDST(&currentProcess->p_s);
        return;
    }

    /* READY QUEUE VUOTA */

    if (processCount == 0) {
        debug_print("All processes completed!\n");
        HALT();
    }

    if (softBlockCount > 0) {
        currentProcess = NULL;

        setMIE(MIE_ALL & ~MIE_MTIE_MASK);

        unsigned int status = getSTATUS();
        status |= MSTATUS_MIE_MASK;
        setSTATUS(status);

        WAIT();
        return;
    }
    /* DEADLOCK */
    if (processCount == 0 && softBlockCount == 0) {
        debug_print("DEADLOCK: no ready processes and no soft-blocked processes!\n");
        HALT();
    }
}
