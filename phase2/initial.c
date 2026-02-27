/*
 * initial.c
 *
 * Punto di entrata del Nucleo (main). Esegue l'inizializzazione una-tantum:
 *   - Dichiara le variabili globali di livello 3
 *   - Popola il Pass Up Vector del processore 0
 *   - Inizializza le strutture dati di livello 2 (Phase 1)
 *   - Inizializza tutte le variabili dichiarate
 *   - Carica l'Interval Timer con 100ms
 *   - Istanzia il processo di test
 *   - Chiama lo Scheduler
 */

#include "../headers/const.h"
#include "../headers/types.h"
#include <uriscv/liburiscv.h>

#include "../phase1/headers/pcb.h"
#include "../phase1/headers/asl.h"
#include "./headers/globals.h"

/* Dichiarazione funzioni esterne */
extern void test();           /* processo di test fornito */
extern void uTLB_RefillHandler();
extern void exceptionHandler();

/* -----------------------------------------------------------------------
 * Definizione variabili globali (dichiarate extern in globals.h)
 * ----------------------------------------------------------------------- */
int              processCount;
int              softBlockCount;
struct list_head readyQueue;
pcb_t           *currentProcess;
int              devSems[TOT_SEMS];
cpu_t            startTOD;


#include "debug.h"
/* -----------------------------------------------------------------------
 * main() - inizializzazione del Nucleo
 * ----------------------------------------------------------------------- */
int main(void) {

    debug_print("\n[BOOT] === Kernel main start ===\n");

    /* ------------------------------------------------------------------
     * 1. Popola il Pass Up Vector
     * ------------------------------------------------------------------ */
    debug_print("[INIT] Setting PassUpVector...\n");

    passupvector_t *passUpVec = (passupvector_t *) PASSUPVECTOR;

    passUpVec->tlb_refill_handler  = (memaddr) uTLB_RefillHandler;
    passUpVec->tlb_refill_stackPtr = KERNELSTACK;

    passUpVec->exception_handler   = (memaddr) exceptionHandler;
    passUpVec->exception_stackPtr  = KERNELSTACK;

    debug_print("[OK] PassUpVector configured.\n");


    /* ------------------------------------------------------------------
     * 2. Inizializza Phase 1
     * ------------------------------------------------------------------ */
    debug_print("[INIT] Initializing PCB and ASL...\n");

    initPcbs();
    initASL();

    debug_print("[OK] Phase 1 structures initialized.\n");


    /* ------------------------------------------------------------------
     * 3. Inizializza variabili globali
     * ------------------------------------------------------------------ */
    debug_print("[INIT] Initializing global variables...\n");

    processCount   = 0;
    softBlockCount = 0;
    currentProcess = NULL;
    mkEmptyProcQ(&readyQueue);

    for (int i = 0; i < TOT_SEMS; i++) {
        devSems[i] = 0;
    }

    startTOD = 0;

    debug_print("[OK] Global variables initialized.\n");


    /* ------------------------------------------------------------------
     * 4. Carica Interval Timer
     * ------------------------------------------------------------------ */
    debug_print("[INIT] Loading Interval Timer (100ms)...\n");

    LDIT(PSECOND);

    debug_print("[OK] Interval Timer loaded.\n");


    /* ------------------------------------------------------------------
     * 5. Crea processo di test
     * ------------------------------------------------------------------ */
    debug_print("[INIT] Allocating test process PCB...\n");

    pcb_t *testPcb = allocPcb();
    if (testPcb == NULL) {
        debug_print("[PANIC] No PCB available!\n");
        PANIC();
    }

    debug_print("[OK] PCB allocated.\n");

    testPcb->p_s.status  = MSTATUS_MPIE_MASK | MSTATUS_MPP_M;
    testPcb->p_s.mie     = MIE_ALL;

    memaddr ramtop;
    RAMTOP(ramtop);

    testPcb->p_s.reg_sp = ramtop;
    testPcb->p_s.pc_epc = (memaddr) test;

    testPcb->p_parent        = NULL;
    testPcb->p_semAdd        = NULL;
    testPcb->p_supportStruct = NULL;
    testPcb->p_time          = 0;
    testPcb->p_prio          = PROCESS_PRIO_LOW;

    insertProcQ(&readyQueue, testPcb);
    processCount++;

    debug_print("[OK] Test process inserted in ReadyQueue.\n");
    debug_print("[INFO] processCount = 1\n");


    /* ------------------------------------------------------------------
     * 6. Avvia scheduler
     * ------------------------------------------------------------------ */
    debug_print("[SCHED] Entering scheduler...\n");

    extern void scheduler();
    scheduler();

    debug_print("[ERROR] Returned from scheduler! (Should never happen)\n");

    return 0;
}
