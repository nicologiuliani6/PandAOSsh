/*
 * initProc.c - Phase 3 / Level 4
 *
 * Implementa l'InstantiatorProcess (test) ed esporta le variabili globali
 * del Support Level. Si occupa di:
 *   - inizializzare le strutture dati del Support Level
 *   - avviare la shell (unica U-proc lanciata direttamente)
 *   - attendere la fine della shell e poi spegnere il sistema (HALT)
 */

#include "headers/support.h"

/* ------------------------------------------------------------------ */
/* Variabili globali del Support Level                                 */
/* ------------------------------------------------------------------ */

int       masterSemaphore;            /* fine InstantiatorProcess (init 0) */
int       shellSemaphore;             /* attesa figlio nella SYS6 (init 0)  */
int       devMutex[DEV_MUTEX_TOTAL];  /* mutua esclusione sui device        */
support_t supportPool[UPROCMAX];      /* una support struct per U-proc      */

/* ------------------------------------------------------------------ */
/* Support structure pool                                              */
/* ------------------------------------------------------------------ */

support_t *getSupport(int asid) {
    return &supportPool[asid - 1];
}

/* ------------------------------------------------------------------ */
/* Avvio di una U-proc                                                 */
/* ------------------------------------------------------------------ */

void launchUproc(int asid) {
    support_t *sup = getSupport(asid);

    /* Identità e Page Table. */
    sup->sup_asid = asid;
    initUprocPageTable(sup);

    /* Context per la gestione delle eccezioni passate su dal Nucleus.
     *  [PGFAULTEXCEPT] -> il Pager (TLB exception handler)
     *  [GENERALEXCEPT] -> il general exception handler                 */
    sup->sup_exceptContext[PGFAULTEXCEPT].pc       = (memaddr) pager;
    sup->sup_exceptContext[PGFAULTEXCEPT].status   = SUPPORT_STATUS;
    sup->sup_exceptContext[PGFAULTEXCEPT].stackPtr =
        (memaddr) &sup->sup_stackTLB[499];

    sup->sup_exceptContext[GENERALEXCEPT].pc       = (memaddr) generalExceptionHandler;
    sup->sup_exceptContext[GENERALEXCEPT].status   = SUPPORT_STATUS;
    sup->sup_exceptContext[GENERALEXCEPT].stackPtr =
        (memaddr) &sup->sup_stackGen[499];

    /* Stato iniziale del processore della U-proc. */
    state_t s;
    for (unsigned int i = 0; i < (STATE_T_SIZE_IN_BYTES / WORDLEN); i++)
        ((unsigned int *)&s)[i] = 0;

    s.pc_epc   = UPROCSTARTADDR;          /* inizio .text (0x800000B0)     */
    s.reg_sp   = USERSTACKTOP;            /* stack a 0xC0000000            */
    s.status   = UPROC_STATUS;            /* user-mode, interrupt + PLT on */
    s.mie      = MIE_ALL;
    s.entry_hi = asid << ASIDSHIFT;       /* ASID univoco della U-proc     */

    /* Creazione della U-proc tramite il Nucleus (NSYS1). */
    SYSCALL(CREATEPROCESS, (int)&s, PROCESS_PRIO_LOW, (int)sup);
}

/* ------------------------------------------------------------------ */
/* InstantiatorProcess (test)                                          */
/* ------------------------------------------------------------------ */

void test(void) {
    /* 1. Strutture dati della memoria virtuale (Swap Pool + semaforo). */
    initSwapStructs();

    /* 2. Semafori del Support Level. */
    masterSemaphore = 0;
    shellSemaphore  = 0;
    for (int i = 0; i < DEV_MUTEX_TOTAL; i++)
        devMutex[i] = 1;

    /* 3. Avvio della shell (ASID 1). */
    launchUproc(1);

    /* 4. Attesa della terminazione della shell. */
    SYSCALL(PASSEREN, (int)&masterSemaphore, 0, 0);

    /* 5. Conclusione: NSYS2 porta il Process Count a 0 -> HALT. */
    SYSCALL(TERMPROCESS, 0, 0, 0);
}
