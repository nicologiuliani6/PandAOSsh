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


static void debug_print(const char *msg) {
    unsigned int *command = (unsigned int *)(0x10000254 + 3*4);
    
    while (*msg != '\0') {
        *command = 2 | (((unsigned int)*msg) << 8);
        /* delay */
        for (volatile int i = 0; i < 10000; i++);
        msg++;
    }
}
/* -----------------------------------------------------------------------
 * main() - inizializzazione del Nucleo
 * ----------------------------------------------------------------------- */
int main(void) {
    debug_print("1");
    /* ------------------------------------------------------------------
     * 1. Popola il Pass Up Vector del processore 0
     *    Il Pass Up Vector del processore 0 si trova a 0x0FFF.F900
     * ------------------------------------------------------------------ */
    passupvector_t *passUpVec = (passupvector_t *) PASSUPVECTOR;

    /* Handler per eventi TLB-Refill */
    passUpVec->tlb_refill_handler    = (memaddr) uTLB_RefillHandler;
    passUpVec->tlb_refill_stackPtr   = KERNELSTACK;

    /* Handler per tutte le altre eccezioni (interrupt inclusi) */
    passUpVec->exception_handler    = (memaddr) exceptionHandler;
    passUpVec->exception_stackPtr   = KERNELSTACK;
    debug_print("2");
    /* ------------------------------------------------------------------
     * 2. Inizializza le strutture dati di livello 2 (Phase 1)
     * ------------------------------------------------------------------ */
    initPcbs();
    initASL();
    debug_print("3");
    /* ------------------------------------------------------------------
     * 3. Inizializza le variabili globali di livello 3
     * ------------------------------------------------------------------ */
    processCount   = 0;
    softBlockCount = 0;
    currentProcess = NULL;
    mkEmptyProcQ(&readyQueue);

    /* Tutti i semafori device/pseudo-clock inizializzati a 0
     * (semafori di sincronizzazione, non di mutua esclusione) */
    for (int i = 0; i < TOT_SEMS; i++) {
        devSems[i] = 0;
    }

    startTOD = 0;
    debug_print("4");
    /* ------------------------------------------------------------------
     * 4. Carica l'Interval Timer con 100ms (PSECOND)
     *    Genera i Pseudo-clock tick ogni 100ms
     * ------------------------------------------------------------------ */
    LDIT(PSECOND);
    debug_print("5");
    /* ------------------------------------------------------------------
     * 5. Istanzia il processo di test
     *    - Alloca un PCB
     *    - Stato iniziale: kernel-mode, interrupt abilitati,
     *      SP = RAMTOP (ultimo frame RAM), PC = test
     *    - Inserisce nella Ready Queue, incrementa processCount
     * ------------------------------------------------------------------ */
    pcb_t *testPcb = allocPcb();
    if (testPcb == NULL) {
        /* Non dovrebbe mai accadere: nessun PCB disponibile */
        PANIC();
    }
    /* Inizializza lo stato del processore per il processo test */
    /* Kernel mode + interrupt abilitati tramite i campi status e mie */
    testPcb->p_s.status  = MSTATUS_MPIE_MASK | MSTATUS_MPP_M;
    testPcb->p_s.mie     = MIE_ALL;
    /* Calcola RAMTOP a runtime: indirizzo di fine RAM
     * In µRISC-V: RAMTOP = RAMBASE + RAMSIZE * PAGESIZE
     * I valori si leggono dalle costanti del BIOS */
    memaddr ramtop;
    RAMTOP(ramtop);

    testPcb->p_s.reg_sp  = ramtop;                        /* stack in cima alla RAM */
    testPcb->p_s.pc_epc  = (memaddr) test;           /* PC punta alla funzione test */

    /* Campi PCB */
    testPcb->p_parent       = NULL;
    testPcb->p_semAdd       = NULL;
    testPcb->p_supportStruct = NULL;
    testPcb->p_time         = 0;
    testPcb->p_prio         = PROCESS_PRIO_LOW;      /* priorità di default */

    /* Inserisce nella Ready Queue come figlio "radice" (no parent) */
    insertProcQ(&readyQueue, testPcb);
    processCount++;
    debug_print("6");
    /* ------------------------------------------------------------------
     * 6. Chiama lo Scheduler - da qui non si ritorna mai
     * ------------------------------------------------------------------ */
    extern void scheduler();
    scheduler();

    /* Non si dovrebbe mai arrivare qui */  
    debug_print("fine main");
    return 0;
}