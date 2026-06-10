/*
 * exceptions.c
 */

#include "../headers/const.h"
#include "../headers/types.h"
#include <uriscv/liburiscv.h>
#include <uriscv/cpu.h>

#include "../phase1/headers/pcb.h"
#include "../phase1/headers/asl.h"
#include "./headers/globals.h"
#include "debug.h"
#include "../headers/listx.h"


#ifndef CAUSE_EXCCODE_MASK
#define CAUSE_EXCCODE_MASK 0xFFu
#endif

#if DEBUG_EXC
#define EDBG(msg)         debug_print(msg)
#define EDBG_HEX(msg,val) debug_hex(msg,val)
#else
#define EDBG(msg)         ((void)0)
#define EDBG_HEX(msg,val) ((void)0)
#endif


static void syscallHandler(state_t *savedState);
static void tlbExceptionHandler(void);
static void programTrapHandler(void);
static void passUpOrDie(int exceptionType);
static void terminateProcess(pcb_t *proc);
static void updateCPUTime(void);
static void blockCurrentProcess(int *sem);
static void copyState(state_t *dst, state_t *src);

extern void scheduler(void);
extern void interruptHandler(void);

/* ------------------------------------------------------------------ */
/* TLB-Refill event handler                                            */
/*                                                                     */
/* Gira in kernel-mode, interrupt disabilitati, sullo stack del Nucleus*/
/* (primo frame di RAM). In phase 2 (nessuna support struct) si comporta*/
/* come lo skeleton fornito dal tester; in phase 3 (SUPPORT_LEVEL) usa  */
/* la Page Table della U-proc corrente per ricaricare l'entry mancante. */
/* ------------------------------------------------------------------ */
void uTLB_RefillHandler(void) {
    state_t *savedState = (state_t *) BIOSDATAPAGE;

#ifdef SUPPORT_LEVEL
    /* Indice [0..31] della pagina mancante nella Page Table. */
    unsigned int vpn = (savedState->entry_hi >> VPNSHIFT) & 0xFFFFF;
    int index;
    if (vpn == 0xBFFFF)
        index = 31;                       /* pagina di stack */
    else
        index = (int)(vpn - 0x80000);     /* pagine .text/.data 0..30 */

    pteEntry_t *pte = &currentProcess->p_supportStruct->sup_privatePgTbl[index];
    setENTRYHI(pte->pte_entryHI);
    setENTRYLO(pte->pte_entryLO);
    TLBWR();
    LDST(savedState);
#else
    /* phase 2: skeleton placeholder (il p5 del tester scrive a 0x80000000). */
    setENTRYHI(0x80000000);
    setENTRYLO(0x00000000);
    TLBWR();
    LDST(savedState);
#endif
}

/*copia dello stato da dst su src*/
static void copyState(state_t *dst, state_t *src) {
    unsigned int *d = (unsigned int *) dst;
    unsigned int *s = (unsigned int *) src;
    for (int i = 0; i < STATE_T_SIZE_IN_BYTES / WORDLEN; i++)
        d[i] = s[i];
}
/* traccia del tempo di cpu del singolo processo*/
static void updateCPUTime(void) {
    if (currentProcess) {
        cpu_t now;
        STCK(now);
        currentProcess->p_time += now - startTOD;
        startTOD = now;
    }
}

/* TRUE solo per i semafori di decopyStatevice, NON per il pseudo-clock */
static int isDeviceSemaphore(int *semAdd) {
    int *base = &devSems[0];
    int *top  = &devSems[TOT_SEMS];

    if (semAdd < base || semAdd >= top) return 0;

    int idx = (int)(semAdd - base);
    if (idx == PSEUDOCLK_SEM) return 0;

    return 1;
}
/*funzione che permetta al kernel di bloccare un processo perche aspetta un evento*/
static void blockCurrentProcess(int *sem) {
    if (!currentProcess) PANIC();

    cpu_t now;
    STCK(now);
    currentProcess->p_time += now - startTOD;
    startTOD = now;

    currentProcess->p_semAdd = sem;
    if (isDeviceSemaphore(sem)) {
        softBlockCount++;
    }

    EDBG_HEX("[BLK] PID bloccato=", (unsigned int)currentProcess->p_pid);
    EDBG_HEX("[BLK] sem addr=", (unsigned int)sem);
    EDBG_HEX("[BLK] sem val=", (unsigned int)*sem);
    EDBG_HEX("[BLK] softBlockCount=", (unsigned int)softBlockCount);
    EDBG_HEX("[BLK] processCount=", (unsigned int)processCount);

    insertBlocked(sem, currentProcess);
    currentProcess = NULL;
    scheduler();
}
/* aggiunge processo a ready queue*/
static void activeProcs_add(pcb_t *p) {
    for (int i = 0; i < MAXPROC; i++) {
        if (activeProcs[i] == NULL) {
            activeProcs[i] = p;
            return;
        }
    }
    PANIC();
}
/* rimuove processo permanentemente dal SO*/
static void activeProcs_remove(pcb_t *p) {
    for (int i = 0; i < MAXPROC; i++) {
        if (activeProcs[i] == p) {
            activeProcs[i] = NULL;
            return;
        }
    }
}
/* cerca un processo dato pid*/
static pcb_t *findProcessByPid(int target) {
    for (int i = 0; i < MAXPROC; i++) {
        if (activeProcs[i] != NULL && activeProcs[i]->p_pid == target)
            return activeProcs[i];
    }
    return NULL;
}
/* syscall sys2: uccide padre e figli*/
static void terminateProcess(pcb_t *p) {
    if (p == NULL) return;

    if (p->p_pid == -1) return;

    EDBG_HEX("[TERM] Termino PID=", (unsigned int)p->p_pid);

    p->p_pid = -1;

    pcb_t *child;
    while ((child = removeChild(p)) != NULL) {
        terminateProcess(child);
    }

    /* Stacca p dalla lista figli del proprio padre. Senza questo, un
     * processo che termina da solo (TERMPROCESS su se stesso) resterebbe
     * agganciato come "figlio fantasma" del padre: alla terminazione del
     * padre, removeChild continuerebbe a restituirlo all'infinito. */
    outChild(p);

    activeProcs_remove(p);

    if (p->p_semAdd != NULL) {
        int *sem = p->p_semAdd;

        EDBG_HEX("[TERM] era bloccato su sem addr=", (unsigned int)sem);
        EDBG_HEX("[TERM] sem val prima=", (unsigned int)*sem);

        pcb_t *removed = outBlocked(p);
        p->p_semAdd = NULL;

        if (removed != NULL) {
            if (isDeviceSemaphore(sem) || sem == &devSems[PSEUDOCLK_SEM]) {
                /* Semaforo di device o pseudo-clock: il processo era
                 * soft-blocked. Il valore del semaforo NON va toccato
                 * (sarà l'interrupt del device / interval timer a fare la V);
                 * basta aggiornare il contatore dei soft-blocked. */
                softBlockCount--;
            } else {
                /* Semaforo "normale" (mutex/sincronizzazione): la P che il
                 * processo terminato aveva eseguito va annullata, altrimenti
                 * il valore resta sbilanciato e altri processi si bloccano
                 * per sempre. */
                (*sem)++;
            }
        }

        EDBG_HEX("[TERM] sem val dopo=", (unsigned int)*sem);
    }

    outProcQ(&readyQueue, p);

    if (p == currentProcess) {
        currentProcess = NULL;
    }

    freePcb(p);
    processCount--;

    EDBG_HEX("[TERM] processCount ora=", (unsigned int)processCount);
    EDBG_HEX("[TERM] softBlockCount ora=", (unsigned int)softBlockCount);
}
/* gestione del TLB su addr. non valido (tlb miss)*/
static void tlbExceptionHandler(void) {
    passUpOrDie(PGFAULTEXCEPT);
}
/* handler eccezioni generali (non tlb miss e syscall)*/
static void programTrapHandler(void) {
    passUpOrDie(GENERALEXCEPT);
}
/* punto di ingresso di tutte le eccezioni del kernel, smista gli errori*/
void exceptionHandler(void) {
    state_t *savedState = (state_t *) BIOSDATAPAGE;
    unsigned int cause   = savedState->cause;

    updateCPUTime();

    /* Bit piu significativo di cause acceso => interrupt */
    if (cause & 0x80000000) {
        interruptHandler();
        return;
    }

    /* Per le eccezioni (non interrupt) il tipo e' dato dal solo ExcCode */
    unsigned int excCode = cause & CAUSE_EXCCODE_MASK;

    switch (excCode) {
        /* ECALL da U-mode (8) o da M-mode (11): chiamata di sistema */
        case EXC_ECU:   /* 8  */
        case EXC_ECM:   /* 11 */
            /* il PC va avanzato di una word per non rieseguire la ecall */
            savedState->pc_epc += WORDLEN;
            syscallHandler(savedState);
            break;

        /* Eccezioni del TLB / gestione memoria:
         *  - page fault standard RISC-V: Instruction (12), Load (13), Store (15)
         *  - codici specifici uRISCV: MOD (24), TLBL (25), TLBS (26),
         *    UTLBL (27), UTLBS (28)
         * Tutte vengono trattate come "memory management trap". */
        case EXC_IPF:   /* 12 */
        case EXC_LPF:   /* 13 */
        case EXC_SPF:   /* 15 */
        case EXC_MOD:   /* 24 */
        case EXC_TLBL:  /* 25 */
        case EXC_TLBS:  /* 26 */
        case EXC_UTLBL: /* 27 */
        case EXC_UTLBS: /* 28 */
            tlbExceptionHandler();
            break;

        /* Tutto il resto (misallineamenti, access fault, istruzione
         * illegale, breakpoint, ...) e' un Program Trap */
        default:
            programTrapHandler();
            break;
    }
}
/* gestisce le chiamate di sistema (quando un proc. ha bisogno di qualcosa dal kernel)*/
static void syscallHandler(state_t *savedState) {
    int sysCode = (int) savedState->reg_a0;

    EDBG_HEX("[SYS] sysCode=", (unsigned int)sysCode);
    if (currentProcess)
        EDBG_HEX("[SYS] currentPID=", (unsigned int)currentProcess->p_pid);

    if ((savedState->status & MSTATUS_MPP_MASK) == 0 && sysCode < 0) {
        savedState->cause = PRIVINSTR;
        programTrapHandler();
        return;
    }

    if (sysCode >= 1) {
        passUpOrDie(GENERALEXCEPT);
        return;
    }
    /* implementazione delle varie syscall*/
    switch (sysCode) {
        case CREATEPROCESS: {
            state_t   *newState = (state_t *) savedState->reg_a1;
            int        prio     = (int) savedState->reg_a2;
            support_t *support  = (support_t *) savedState->reg_a3;

            pcb_t *child = allocPcb();
            if (!child) {
                savedState->reg_a0 = (unsigned int) -1;
                copyState(&currentProcess->p_s, savedState);
                LDST(&currentProcess->p_s);
            }

            /* allocPcb ha già azzerato p_time e p_semAdd */
            copyState(&child->p_s, newState);
            child->p_supportStruct = support;
            child->p_prio          = prio;

            activeProcs_add(child);
            insertChild(currentProcess, child);
            processCount++;

            EDBG_HEX("[CREATE] nuovo PID=", (unsigned int)child->p_pid);
            EDBG_HEX("[CREATE] prio=", (unsigned int)prio);
            EDBG_HEX("[CREATE] processCount=", (unsigned int)processCount);

            savedState->reg_a0 = (unsigned int) child->p_pid;
            copyState(&currentProcess->p_s, savedState);
            /* Mettiamo in ready queue sia padre che figlio e lasciamo allo
             * scheduler la scelta in base alla priorità: così, se il figlio
             * è più prioritario, parte lui per primo. */
            insertProcQ(&readyQueue, currentProcess);
            insertProcQ(&readyQueue, child);
            currentProcess = NULL;
            scheduler();
            break;
        }

        case TERMPROCESS: {
            int targetPid = (int) savedState->reg_a1;
            updateCPUTime();

            EDBG_HEX("[TERMPROCESS] targetPid=", (unsigned int)targetPid);

            pcb_t *target = (targetPid == 0) ? currentProcess : findProcessByPid(targetPid);
            if (!target) {
                //EDBG("[TERMPROCESS] target non trovato, LDST\n");
                LDST(savedState);
            } else {
                int terminatingSelf = (target == currentProcess);
                terminateProcess(target);
                if (terminatingSelf) currentProcess = NULL;
            }

            if (currentProcess == NULL) {
                scheduler();
            } else {
                copyState(&currentProcess->p_s, savedState);
                LDST(&currentProcess->p_s);
            }
            break;
        }

        case PASSEREN: {
            int *semAddr = (int *) savedState->reg_a1;

            updateCPUTime();
            copyState(&currentProcess->p_s, savedState);

            (*semAddr)--;

            EDBG_HEX("[P] sem addr=", (unsigned int)semAddr);
            EDBG_HEX("[P] sem val dopo--=", (unsigned int)*semAddr);
            EDBG_HEX("[P] PID=", (unsigned int)currentProcess->p_pid);

            if (*semAddr < 0) {
                EDBG("[P] processo si blocca\n");
                blockCurrentProcess(semAddr);
            }

            LDST(&currentProcess->p_s);
            break;
        }

        case VERHOGEN: {
            int *semAddr = (int *) savedState->reg_a1;

            updateCPUTime();
            copyState(&currentProcess->p_s, savedState);

            (*semAddr)++;

            EDBG_HEX("[V] sem addr=", (unsigned int)semAddr);
            EDBG_HEX("[V] sem val dopo++=", (unsigned int)*semAddr);
            EDBG_HEX("[V] PID=", (unsigned int)currentProcess->p_pid);

            if (*semAddr <= 0) {
                pcb_t *unblocked = removeBlocked(semAddr);
                if (unblocked) {
                    EDBG_HEX("[V] sbloccato PID=", (unsigned int)unblocked->p_pid);
                    unblocked->p_semAdd = NULL;
                    insertProcQ(&readyQueue, unblocked);
                }
            }

           

            LDST(&currentProcess->p_s);
            break;
        }

        case DOIO: {
            int *commandAddr  = (int *) savedState->reg_a1;
            int  commandValue = (int) savedState->reg_a2;

            unsigned int offset = (unsigned int)commandAddr - START_DEVREG;
            int line    = (int)(offset / 0x80) + 3;
            int dev     = (int)((offset % 0x80) / 0x10);
            int subword = (int)((offset % 0x10) / WORDLEN);

            int semIdx = (line == 7)
                ? ((subword == 3) ? TERM_TX_SEM(dev) : TERM_RX_SEM(dev))
                : DEV_SEM_BASE(line, dev);

            updateCPUTime();
            copyState(&currentProcess->p_s, savedState);

            /* Emette il comando al device e blocca SEMPRE il processo: ogni
             * operazione di I/O è asincrona e si conclude con un interrupt di
             * completamento, che risveglia il processo impostandone reg_a0 al
             * device status. Il Nucleus gira con gli interrupt disabilitati,
             * quindi non c'è race tra l'emissione del comando e il blocco:
             * l'interrupt resta pending finché lo scheduler non fa WAIT/LDST. */
            *commandAddr = commandValue;
            devSems[semIdx]--;
            blockCurrentProcess(&devSems[semIdx]);
            break;
        }

        case GETTIME: {
            updateCPUTime();
            savedState->reg_a0 = (unsigned int) currentProcess->p_time;
            LDST(savedState);
            break;
        }

        case CLOCKWAIT: {
            updateCPUTime();
            copyState(&currentProcess->p_s, savedState);
            devSems[PSEUDOCLK_SEM]--;
            softBlockCount++;
            EDBG_HEX("[CLOCKWAIT] PID=", (unsigned int)currentProcess->p_pid);
            EDBG_HEX("[CLOCKWAIT] softBlockCount=", (unsigned int)softBlockCount);
            blockCurrentProcess(&devSems[PSEUDOCLK_SEM]);
            break;
        }

        case GETSUPPORTPTR: {
            savedState->reg_a0 = (unsigned int) currentProcess->p_supportStruct;
            LDST(savedState);
            break;
        }

        case GETPROCESSID: {
            int wantParent = (int) savedState->reg_a1;
            savedState->reg_a0 = (wantParent == 0)
                ? (unsigned int) currentProcess->p_pid
                : (currentProcess->p_parent ? (unsigned int) currentProcess->p_parent->p_pid : 0);
            LDST(savedState);
            break;
        }

        case YIELD: {
            updateCPUTime();
            copyState(&currentProcess->p_s, savedState);
            EDBG_HEX("[YIELD] PID=", (unsigned int)currentProcess->p_pid);
            /* Non rimettiamo subito il processo in ready queue: lo
             * affidiamo allo scheduler tramite yieldedProcess, così
             * un eventuale processo pronto di pari priorità gira prima. */
            yieldedProcess = currentProcess;
            currentProcess = NULL;
            scheduler();
            break;
        }
        /* eccezioni restanti*/
        default:
            passUpOrDie(GENERALEXCEPT);
    }
}
/* implementa meccaiscmo per passupvector*/
static void passUpOrDie(int exceptionType) {
    if (!currentProcess || !currentProcess->p_supportStruct) {
        EDBG_HEX("[PASSUPDIE] termino PID=", currentProcess ? (unsigned int)currentProcess->p_pid : 0xDEAD);
        EDBG_HEX("[PASSUPDIE] exceptionType=", (unsigned int)exceptionType);
        updateCPUTime();
        terminateProcess(currentProcess);
        currentProcess = NULL;
        scheduler();
    } else {
        support_t *sup = currentProcess->p_supportStruct;
        copyState(&sup->sup_exceptState[exceptionType], (state_t *) BIOSDATAPAGE);
        context_t *ctx = &sup->sup_exceptContext[exceptionType];
        LDCXT(ctx->stackPtr, ctx->status, ctx->pc);
    }
}