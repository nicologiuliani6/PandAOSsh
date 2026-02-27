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
void scheduler(void) {

    debug_print("\n[SCHED] ===== Scheduler invoked =====\n");

    /* ================================================================ */
    /* Caso 1: Ready Queue non vuota → dispatch del primo processo      */
    /* ================================================================ */
    if (!emptyProcQ(&readyQueue)) {

        debug_print("[SCHED] ReadyQueue not empty: dispatching first process\n");

        currentProcess = removeProcQ(&readyQueue);
        if (currentProcess == NULL) {
            debug_print("[PANIC] ReadyQueue returned NULL PCB!\n");
            PANIC();
        }

        debug_print("[SCHED] Current process set\n");

        /* Registra il TOD per calcolare p_time */
        STCK(startTOD);
        debug_hex("[SCHED] startTOD=", startTOD);

        /* Carica PLT con timeslice */
        setTIMER(TIMESLICE * (*((cpu_t *)TIMESCALEADDR)));
        debug_hex("[SCHED] TIMESLICE * TIMESCALEADDR=", TIMESLICE * (*((cpu_t *)TIMESCALEADDR)));

        /* Debug informazioni sul processo */
        debug_hex("[SCHED] Dispatching process pc_epc=", (unsigned int)currentProcess->p_s.pc_epc);
        debug_hex("[SCHED] Dispatching process reg_sp=", (unsigned int)currentProcess->p_s.reg_sp);
        debug_hex("[SCHED] Dispatching process status=", (unsigned int)currentProcess->p_s.status);

        debug_print("[SCHED] Loading process state and giving control...\n");
        LDST(&(currentProcess->p_s));

        /* Non si dovrebbe mai arrivare qui */
        debug_print("[SCHED] ERROR: returned from LDST!\n");
        return;
    }

    /* ================================================================ */
    /* Ready Queue vuota                                                */
    /* ================================================================ */
    debug_print("[SCHED] ReadyQueue empty\n");
    debug_hex("[SCHED] processCount=", processCount);
    debug_hex("[SCHED] softBlockCount=", softBlockCount);

    /* Nessun processo rimasto */
    if (processCount == 0) {
        debug_print("[SCHED] No processes left: HALT\n");
        HALT();
    }

    /* Ci sono processi bloccati: Wait State */
    if (softBlockCount > 0) {

        debug_print("[SCHED] Processes blocked: entering WAIT state\n");

        currentProcess = NULL;

        /* Abilita interrupt globali, disabilita PLT */
        setMIE(MIE_ALL & ~MIE_MTIE_MASK);
        unsigned int status = getSTATUS();
        status |= MSTATUS_MIE_MASK;
        setSTATUS(status);

        debug_print("[SCHED] WAIT instruction: processor idle until interrupt\n");
        WAIT();

        /* Non si dovrebbe mai tornare qui */
        debug_print("[SCHED] ERROR: returned from WAIT!\n");
        return;
    }

    /* Caso deadlock */
    debug_print("[SCHED] DEADLOCK detected: no ready processes, but some soft-blocked processes\n");
    PANIC();
}
