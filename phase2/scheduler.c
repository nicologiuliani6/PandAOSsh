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

#include "debug.h"

/*
 * scheduler()
 *
 * Seleziona il prossimo processo dalla Ready Queue e lo dispatcha,
 * oppure gestisce il caso in cui la Ready Queue sia vuota.
 */
#define PANIC() //while(1) continue
void scheduler(void) {

    /* Caso 1: Ready Queue non vuota → dispatch del primo processo */
    if (!emptyProcQ(&readyQueue)) {

        currentProcess = removeProcQ(&readyQueue);
        if (!currentProcess) PANIC(); // PCB invalido: loop infinito

        /* Armare il PLT prima di LDST */
        setTIMER(TIMESLICE * (*((cpu_t *)TIMESCALEADDR)));

        LDST(&(currentProcess->p_s));
        return;
    }

    /* Ready Queue vuota */
    if (processCount == 0) {
        debug_print("[SCHED] No processes left: HALT\n");
        HALT();
    }

    if (softBlockCount > 0) {
        currentProcess = NULL;
        setMIE(MIE_ALL & ~MIE_MTIE_MASK);
        unsigned int status = getSTATUS();
        status |= MSTATUS_MIE_MASK;
        setSTATUS(status);
        WAIT(); // processor idle
        return;
    }

    /* Deadlock: nessun processo pronto ma soft-blocked > 0 */
    debug_print("[SCHED] DEADLOCK detected\n");
    PANIC();
}