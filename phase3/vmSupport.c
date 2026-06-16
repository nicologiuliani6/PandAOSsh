/*
 * vmSupport.c - Phase 3 / Level 4
 *
 * Gestione della memoria virtuale del Support Level:
 *   - Swap Pool table e relativo semaforo di mutua esclusione
 *   - il Pager (TLB exception handler per i page fault)
 *   - lettura/scrittura del backing store (device flash)
 *   - inizializzazione della Page Table di una U-proc
 */

#include "headers/support.h"

/* Variabili globali della memoria virtuale*/
int    swapPoolSem;
swap_t swapPool[SWAP_POOL_SIZE];

/* Indice FIFO per l'algoritmo di rimpiazzo pagine (round robin).*/
static int fifoNext = 0;

/* Utility*/

/* Abilita/disabilita gli interrupt per rendere atomico l'aggiornamento
 * combinato di Page Table + TLB. */
static inline void interruptsOff(void) {
    setSTATUS(getSTATUS() & ~MSTATUS_MIE_MASK);
}
static inline void interruptsOn(void) {
    setSTATUS(getSTATUS() | MSTATUS_MIE_MASK);
}

/* Dato l'EntryHI salvato, ritorna l'indice [0..31] nella Page Table.
 * Le pagine 0..30 corrispondono ai VPN 0x80000..0x8001E, la pagina di
 * stack (indice 31) al VPN 0xBFFFF. Ritorna -1 se il VPN è fuori range. */
static int vpnToIndex(unsigned int entryHI) {
    unsigned int vpn = (entryHI >> VPNSHIFT) & 0xFFFFF;
    if (vpn == KUSEG_STACK_VPN)
        return UPROC_STACKPAGE;
    if (vpn >= KUSEG_VPN_START && vpn < KUSEG_VPN_START + UPROC_TEXTPAGES)
        return (int)(vpn - KUSEG_VPN_START);
    return -1;
}

/* Indirizzo fisico del frame i dello Swap Pool.*/
static inline memaddr frameAddr(int i) {
    return (memaddr)(SWAP_POOL_START + i * PAGESIZE);
}

/* Invalida un'entry di Page Table e azzera il TLB in modo atomico.*/
static void markPageNotValid(pteEntry_t *pte) {
    interruptsOff();
    pte->pte_entryLO &= ~VALIDON;
    TLBCLR();
    interruptsOn();
}

/* Rende presente un'entry di Page Table (PFN + V) e aggiorna il TLB in
 * modo atomico. Oltre ad azzerare le entry stantie con TLBCLR, l'entry
 * appena resa valida viene scritta DIRETTAMENTE nel TLB (TLBWR): così
 * l'accesso che riprende subito dopo trova già la traduzione valida senza
 * dover passare per un evento di TLB-Refill (che in alcune situazioni non
 * viene rigenerato, lasciando il processo in page-fault loop). */
static void markPagePresent(pteEntry_t *pte, memaddr phys) {
    interruptsOff();
    pte->pte_entryLO = phys | DIRTYON | VALIDON;
    TLBCLR();
    setENTRYHI(pte->pte_entryHI);
    setENTRYLO(pte->pte_entryLO);
    TLBWR();
    interruptsOn();
}

/* Inizializzazione*/                                                    
void initSwapStructs(void) {
    swapPoolSem = 1;
    fifoNext    = 0;
    for (int i = 0; i < SWAP_POOL_SIZE; i++)
        swapPool[i].sw_asid = SWAP_FRAME_FREE;
}

void initUprocPageTable(support_t *sup) {
    int asid = sup->sup_asid;
    for (int i = 0; i < UPROC_TEXTPAGES; i++) {
        sup->sup_privatePgTbl[i].pte_entryHI =
            ((KUSEG_VPN_START + i) << VPNSHIFT) | (asid << ASIDSHIFT);
        sup->sup_privatePgTbl[i].pte_entryLO = DIRTYON; /* V=0: non presente */
    }
    /* Pagina di stack (indice 31). */
    sup->sup_privatePgTbl[UPROC_STACKPAGE].pte_entryHI =
        (KUSEG_STACK_VPN << VPNSHIFT) | (asid << ASIDSHIFT);
    sup->sup_privatePgTbl[UPROC_STACKPAGE].pte_entryLO = DIRTYON;
}

/* Backing store (device flash)*/

/* Esegue una operazione (FLASHREAD/FLASHWRITE) sul device flash della
 * U-proc identificata da asid, sul blocco blockNo, usando frameAddr come
 * sorgente/destinazione DMA. Ritorna lo status del device.*/
int flashOperation(int asid, int blockNo, memaddr frameAddr_, int op) {
    int        devNo = asid - 1;
    dtpreg_t  *flash = (dtpreg_t *) DEV_REG_ADDR(IL_FLASH, devNo);
    int        mutex = FLASH_MUTEX(devNo);
    unsigned int status;

    SYSCALL(PASSEREN, (int)&devMutex[mutex], 0, 0);

    flash->data0 = frameAddr_;
    /* numero blocco nei 3 byte alti, comando nel byte basso*/
    unsigned int command = ((unsigned int)blockNo << 8) | op;
    status = SYSCALL(DOIO, (int)&flash->command, (int)command, 0);

    SYSCALL(VERHOGEN, (int)&devMutex[mutex], 0, 0);
    return (int)status;
}

/*Pager*/

void pager(void) {
    support_t *sup = (support_t *) SYSCALL(GETSUPPORTPTR, 0, 0, 0);
    state_t   *exState = &sup->sup_exceptState[PGFAULTEXCEPT];

    unsigned int excCode = exState->cause & CAUSE_EXCCODE_MASK;

    /* TLB-Modification: una pagina marcata read-only è stata scritta.
     * In PandOSsh non dovrebbe accadere: trattalo come program trap.*/
    if (excCode == EXC_MOD) {
        supTerminate(sup->sup_asid);
        return;
    }

    /* Mutua esclusione sullo Swap Pool.*/
    SYSCALL(PASSEREN, (int)&swapPoolSem, 0, 0);

    /* Pagina mancante. */
    int p = vpnToIndex(exState->entry_hi);
    if (p < 0) {
        /* Indirizzo fuori dallo spazio logico: program trap.*/
        SYSCALL(VERHOGEN, (int)&swapPoolSem, 0, 0);
        supTerminate(sup->sup_asid);
        return;
    }

    /* Scelta del frame (FIFO). */
    int i = fifoNext;
    fifoNext = (fifoNext + 1) % SWAP_POOL_SIZE;
    memaddr fa = frameAddr(i);

    /* Se il frame è occupato, sfratta la pagina che lo abita. */
    if (swapPool[i].sw_asid != SWAP_FRAME_FREE) {
        int        victimAsid = swapPool[i].sw_asid;
        int        victimPage = swapPool[i].sw_pageNo;
        pteEntry_t *victimPte  = swapPool[i].sw_pte;

        /* Aggiorna Page Table + TLB della vittima in modo atomico. */
        markPageNotValid(victimPte);

        /* Scrive il contenuto del frame sul backing store della vittima. */
        int st = flashOperation(victimAsid, victimPage, fa, FLASHWRITE);
        if (st != READY) {
            SYSCALL(VERHOGEN, (int)&swapPoolSem, 0, 0);
            supTerminate(sup->sup_asid);
            return;
        }
    }

    /* Legge la pagina p della U-proc corrente dal suo backing store.*/
    int st = flashOperation(sup->sup_asid, p, fa, FLASHREAD);
    if (st != READY) {
        SYSCALL(VERHOGEN, (int)&swapPoolSem, 0, 0);
        supTerminate(sup->sup_asid);
        return;
    }

    /* Aggiorna la Swap Pool table per il frame i.*/
    swapPool[i].sw_asid   = sup->sup_asid;
    swapPool[i].sw_pageNo = p;
    swapPool[i].sw_pte    = &sup->sup_privatePgTbl[p];

    /* Aggiorna Page Table + TLB della U-proc corrente (atomico).*/
    markPagePresent(&sup->sup_privatePgTbl[p], fa);

    /* Rilascia la mutua esclusione e riprende la U-proc.*/
    SYSCALL(VERHOGEN, (int)&swapPoolSem, 0, 0);
    LDST(exState);
}
