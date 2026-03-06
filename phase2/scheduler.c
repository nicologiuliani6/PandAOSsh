/*
 * scheduler.c
 * Adattato da MicroPandOS (uMPS3/MIPS) per uRISCV.
 *
 * Differenze rispetto a MicroPandOS:
 * - setPLT → setTIMER
 * - getTOD → STCK(startTOD)
 * - HALT quando processCount == 0 (niente SSI, quindi 0 non 1)
 * - WAIT: usa setMIE + setSTATUS invece di setSTATUS(ALLOFF|IECON|IMON)
 */

#include "../headers/const.h"
#include "../headers/types.h"
#include <uriscv/liburiscv.h>

#include "../phase1/headers/pcb.h"
#include "./headers/globals.h"
#include "debug.h"

void scheduler(void) {

    /* 1. Processo pronto: eseguilo
     *    MicroPandOS: setPLT(TIMESLICE*timescale); start=getTOD(); LDST()
     *    uRISCV:      setTIMER(TIMESLICE*timescale); STCK(startTOD); LDST()
     */
    if (!emptyProcQ(&readyQueue)) {
        currentProcess = removeProcQ(&readyQueue);
        if (currentProcess == NULL) PANIC();

        /* Carica PLT per il time slice */
        setTIMER((cpu_t)(TIMESLICE * (*((cpu_t *)TIMESCALEADDR))));

        /* Aggiorna startTOD per il calcolo del CPU time */
        STCK(startTOD);

        LDST(&(currentProcess->p_s));
        /* LDST non ritorna */
    }

    /* 2. Ready queue vuota: deadlock detection
     *    MicroPandOS usava process_count == 1 (SSI sempre vivo).
     *    Noi usiamo processCount == 0 (niente SSI).
     */

    if (processCount == 0) {
        /* Nessun processo vivo: HALT */
        HALT();
    }

    if (processCount > 0 && softBlockCount > 0) {
        /* Processi vivi ma tutti bloccati su I/O: WAIT per interrupt
         * MicroPandOS: setSTATUS(ALLOFF | IECON | IMON)
         * uRISCV: abilita tutti gli interrupt tranne PLT, poi WAIT
         */
        currentProcess = NULL;

        /* Abilita interrupt globali (MIE) e tutti i device interrupt,
         * ma disabilita PLT (MTIE) per non ricevere timeout durante WAIT */
        setMIE(MIE_ALL & ~MIE_MTIE_MASK);
        unsigned int status = getSTATUS();
        status |= MSTATUS_MIE_MASK;
        setSTATUS(status);

        WAIT();
        return;
    }

    /* 3. Processi vivi, nessuno bloccato su I/O, ready queue vuota → DEADLOCK */
    PANIC();
}