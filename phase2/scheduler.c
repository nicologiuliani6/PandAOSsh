#include "../headers/const.h"
#include "../headers/types.h"
#include <uriscv/liburiscv.h>

#include "../phase1/headers/pcb.h"
#include "./headers/globals.h"
#include "debug.h"

#include "../headers/const.h"
#include "../headers/types.h"
#include <uriscv/liburiscv.h>

#include "../phase1/headers/pcb.h"
#include "./headers/globals.h"
#include "debug.h"

void scheduler(void) {

    if (!emptyProcQ(&readyQueue)) {
        currentProcess = removeProcQ(&readyQueue);
        if (currentProcess == NULL) PANIC();
        STCK(startTOD);
        cpu_t plt = TIMESLICE * (*((cpu_t *)TIMESCALEADDR));
        setTIMER(plt);
        LDST(&(currentProcess->p_s));
        return;
    }

    if (processCount == 0) {
        HALT();
    }

    if (softBlockCount > 0 || processCount > 0) {
        currentProcess = NULL;
        setMIE(MIE_ALL & ~MIE_MTIE_MASK);
        unsigned int status = getSTATUS();
        status |= MSTATUS_MIE_MASK;
        setSTATUS(status);
        WAIT();
        return;
    }

    PANIC();
}