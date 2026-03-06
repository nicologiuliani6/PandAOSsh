#include "../headers/const.h"
#include "../headers/types.h"
#include <uriscv/liburiscv.h>
#include "../phase1/headers/pcb.h"
#include "../phase1/headers/asl.h"
#include "./headers/globals.h"

extern void test();
extern void uTLB_RefillHandler();
extern void exceptionHandler();
extern void scheduler();

int              processCount;
int              softBlockCount;
struct list_head readyQueue;
pcb_t           *currentProcess;
int              devSems[TOT_SEMS];
cpu_t            startTOD;
pcb_t           *activeProcs[MAXPROC];

int main(void) {
    /* 1. Pass Up Vector */
    passupvector_t *passUpVec = (passupvector_t *) PASSUPVECTOR;
    memaddr ramtop;
    RAMTOP(ramtop);

    passUpVec->tlb_refill_handler  = (memaddr) uTLB_RefillHandler;
    passUpVec->tlb_refill_stackPtr = ramtop;          /* FIX: ramtop riservato agli handler */
    passUpVec->exception_handler   = (memaddr) exceptionHandler;
    passUpVec->exception_stackPtr  = ramtop - PAGESIZE; /* FIX: secondo frame per exception handler */

    /* 2. Phase 1 */
    initPcbs();
    initASL();

    /* 3. Variabili globali */
    processCount   = 0;
    softBlockCount = 0;
    currentProcess = NULL;
    mkEmptyProcQ(&readyQueue);

    for (int i = 0; i < TOT_SEMS; i++)
        devSems[i] = 0;

    /* FIX: leggi il TOD reale invece di azzerarlo */
    STCK(startTOD);

    for (int i = 0; i < MAXPROC; i++)
        activeProcs[i] = NULL;

    /* 4. Interval Timer */
    LDIT(PSECOND);

    /* 5. Processo test */
    pcb_t *testPcb = allocPcb();
    if (testPcb == NULL) PANIC();

    testPcb->p_s.status      = MSTATUS_MIE_MASK | MSTATUS_MPIE_MASK | MSTATUS_MPP_M;
    testPcb->p_s.mie         = MIE_ALL;
    testPcb->p_s.reg_sp      = ramtop - (2 * PAGESIZE); /* FIX: distanza sicura da entrambi gli handler stack */
    testPcb->p_s.pc_epc      = (memaddr) test;
    testPcb->p_parent        = NULL;
    testPcb->p_semAdd        = NULL;
    testPcb->p_supportStruct = NULL;
    testPcb->p_time          = 0;
    testPcb->p_prio          = PROCESS_PRIO_LOW;

    activeProcs[0] = testPcb;
    insertProcQ(&readyQueue, testPcb);
    processCount = 1;

    /* 6. Scheduler */
    scheduler();

    return 0;
}