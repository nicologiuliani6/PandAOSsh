/*
 * exceptions.c
 */

#include "../headers/const.h"
#include "../headers/types.h"
#include <uriscv/liburiscv.h>

#include "../phase1/headers/pcb.h"
#include "../phase1/headers/asl.h"
#include "./headers/globals.h"
#include "debug.h"

/* Prototipi interni */
static void syscallHandler(state_t *savedState);
static void tlbExceptionHandler(void);
static void programTrapHandler(void);
static void passUpOrDie(int exceptionType);
static void terminateProcess(pcb_t *proc);
static void updateCPUTime(void);

extern void scheduler(void);
extern void interruptHandler(void);

/* -----------------------------------------------------------------------
 * Utility
 * ----------------------------------------------------------------------- */

static void copyState(state_t *dst, state_t *src) {
    unsigned int *d = (unsigned int *) dst;
    unsigned int *s = (unsigned int *) src;
    /*
     * Usiamo STATE_T_SIZE_IN_BYTES (148) definito in const.h,
     * che riflette la dimensione reale di state_t in uRISCV.
     * STATESIZE (0x8C = 140) è un retaggio di uMPS3 e non è corretto
     * per uRISCV: usarlo causerebbe la copia di 2 word in meno.
     */
    for (int i = 0; i < STATE_T_SIZE_IN_BYTES / WORDLEN; i++)
        d[i] = s[i];
}

static void updateCPUTime(void) {
    if (currentProcess) {
        cpu_t now;
        STCK(now);
        currentProcess->p_time += now - startTOD;
        STCK(startTOD);
    }
}

/* Ritorna 1 se semAdd punta dentro l'array devSems[], 0 altrimenti */
static int isDeviceSemaphore(int *semAdd) {
    int *base = &devSems[0];
    int *top  = &devSems[TOT_SEMS];
    return (semAdd >= base && semAdd < top);
}

/*
 * Registra un processo nella lista globale activeProcs[].
 * Chiamata subito dopo allocPcb() in CREATEPROCESS.
 */
static void activeProcs_add(pcb_t *p) {
    for (int i = 0; i < MAXPROC; i++) {
        if (activeProcs[i] == NULL) {
            activeProcs[i] = p;
            return;
        }
    }
    debug_print("[PANIC] activeProcs full!\n");
    PANIC();
}

/*
 * Rimuove un processo dalla lista globale activeProcs[].
 * Chiamata in terminateProcess() prima di freePcb().
 */
static void activeProcs_remove(pcb_t *p) {
    for (int i = 0; i < MAXPROC; i++) {
        if (activeProcs[i] == p) {
            activeProcs[i] = NULL;
            return;
        }
    }
}

/*
 * Cerca un processo per PID in activeProcs[].
 * Funziona per qualsiasi processo vivo: running, ready o blocked,
 * indipendentemente dalla posizione nell'albero (risolve il bug
 * di p10 che deve terminare p9, suo padre).
 */
static pcb_t *findProcessByPid(int target) {
    for (int i = 0; i < MAXPROC; i++) {
        if (activeProcs[i] != NULL && activeProcs[i]->p_pid == target)
            return activeProcs[i];
    }
    return NULL;
}

/* -----------------------------------------------------------------------
 * terminateProcess
 * Termina ricorsivamente proc e tutto il suo sottoalbero.
 * ----------------------------------------------------------------------- */
static void terminateProcess(pcb_t *proc) {
    if (!proc) return;

    /* Prima termina tutti i figli ricorsivamente */
    pcb_t *child;
    while ((child = removeChild(proc)) != NULL)
        terminateProcess(child);

    /* Rimuovi dalla lista globale */
    activeProcs_remove(proc);

    /* Se bloccato su un semaforo, rimuovi dall'ASL */
    if (proc->p_semAdd != NULL) {
        outBlocked(proc);
        /* Decrementa softBlockCount solo per device/clock semaphore */
        if (isDeviceSemaphore(proc->p_semAdd))
            softBlockCount--;
    } else if (proc != currentProcess) {
        /* Rimuovi dalla ready queue solo se non è il processo corrente */
        outProcQ(&readyQueue, proc);
    }

    /* Stacca dal padre e libera */
    outChild(proc);
    freePcb(proc);
    processCount--;
}

/* -----------------------------------------------------------------------
 * Exception sub-handlers
 * ----------------------------------------------------------------------- */

static void tlbExceptionHandler(void) {
    passUpOrDie(PGFAULTEXCEPT);
}

static void programTrapHandler(void) {
    passUpOrDie(GENERALEXCEPT);
}

/* -----------------------------------------------------------------------
 * exceptionHandler - entry point principale
 * ----------------------------------------------------------------------- */
void exceptionHandler(void) {
    /* RAW: scrivi 'E' direttamente sul terminale senza chiamate a funzione */
    {
        volatile unsigned int *s = (volatile unsigned int *)0x1000025C;
        volatile unsigned int *c = (volatile unsigned int *)0x10000260;
        while ((*s & 0xFF) == 3); /* wait not BUSY */
        *c = 2 | ('E' << 8);
        while ((*s & 0xFF) == 3); /* wait complete */
    }
    state_t     *savedState = (state_t *) BIOSDATAPAGE;
    unsigned int cause      = savedState->cause;
    unsigned int excCode    = (cause & GETEXECCODE) >> CAUSESHIFT;

    debug_hex("[EXC] cause=",          cause);
    debug_hex("[EXC] excCode=",        excCode);
    debug_hex("[EXC] currentProcess=", (unsigned int)currentProcess);

    if (cause & 0x80000000) {
        debug_print("[EXC] Interrupt -> interruptHandler()\n");
        interruptHandler();
    }
    else if (excCode == 8 || excCode == 11) {
        /* ECALL da U-mode (8) o M-mode (11) */
        debug_hex("[SYSCALL] sysCode=", savedState->reg_a0);
        syscallHandler(savedState);
    }
    else if (excCode >= 24 && excCode <= 28) {
        /* TLB exceptions in uRISCV (custom excCodes, not standard RISC-V):
         * 24-28 as per PandOSsh Phase 2 spec Section 5 */
        debug_print("[EXC] TLB exception\n");
        tlbExceptionHandler();
    }
    else {
        /* Illegal instruction, breakpoint, ecc. */
        debug_print("[EXC] Program Trap\n");
        programTrapHandler();
    }
}

/* -----------------------------------------------------------------------
 * syscallHandler
 * ----------------------------------------------------------------------- */
static void syscallHandler(state_t *savedState) {
    int sysCode = (int) savedState->reg_a0;
    debug_hex("[SYSCALL] sysCode=", (unsigned int)sysCode);
    debug_hex("[SYSCALL] currentProcess PID=",
              currentProcess ? (unsigned int)currentProcess->p_pid : 0);

    /* Syscall negativa da user mode → program trap (PRIVINSTR) */
    if ((savedState->status & MSTATUS_MPP_MASK) == 0 && sysCode < 0) {
        debug_print("[SYSCALL] Privileged syscall in user mode\n");
        savedState->cause = (savedState->cause & CLEAREXECCODE)
                            | (PRIVINSTR << CAUSESHIFT);
        programTrapHandler();
        return;
    }

    /* Syscall >= 1: non gestita dal kernel → passUpOrDie */
    if (sysCode >= 1) {
        debug_print("[SYSCALL] sysCode >= 1 -> passUpOrDie\n");
        passUpOrDie(GENERALEXCEPT);
        return;
    }

    switch (sysCode) {

        /* ----------------------------------------------------------------
         * SYS1 - CREATEPROCESS
         * a1 = state_t*   stato iniziale del figlio
         * a2 = int        priorità
         * a3 = support_t* struttura supporto (NULL se non serve)
         * ret a0 = PID figlio, -1 se fallisce
         * ---------------------------------------------------------------- */
        case CREATEPROCESS: {
            debug_print("[SYSCALL] CREATEPROCESS\n");
            state_t   *newState = (state_t *)  savedState->reg_a1;
            int        prio     = (int)         savedState->reg_a2;
            support_t *support  = (support_t *) savedState->reg_a3;

            pcb_t *child = allocPcb();
            if (!child) {
                debug_print("[SYSCALL] CREATEPROCESS: no PCB\n");
                savedState->reg_a0 = (unsigned int) -1;
            } else {
                copyState(&child->p_s, newState);
                child->p_supportStruct = support;
                child->p_time          = 0;
                child->p_semAdd        = NULL;
                child->p_prio          = prio;

                activeProcs_add(child);
                insertProcQ(&readyQueue, child);
                insertChild(currentProcess, child);
                processCount++;

                debug_hex("[SYSCALL] Created PID=", (unsigned int)child->p_pid);
                savedState->reg_a0 = (unsigned int) child->p_pid;
            }

            savedState->pc_epc += WORDLEN;
            LDST(savedState);
            break;
        }

        /* ----------------------------------------------------------------
         * SYS2 - TERMPROCESS
         * a1 = pid  (0 = processo corrente)
         *
         * Cerca il target in activeProcs[] per PID: funziona per qualsiasi
         * processo vivo, incluso il padre (caso p10 → termina p9).
         * Terminare un processo termina anche tutto il suo sottoalbero.
         * ---------------------------------------------------------------- */
        case TERMPROCESS: {
            debug_print("[SYSCALL] TERMPROCESS\n");
            int targetPid = (int) savedState->reg_a1;
            debug_hex("[SYSCALL] targetPid=", (unsigned int)targetPid);
            updateCPUTime();

            if (targetPid == 0) {
                terminateProcess(currentProcess);
                currentProcess = NULL;
            } else {
                pcb_t *target = findProcessByPid(targetPid);
                if (target) {
                    int terminatingSelf = (target == currentProcess);
                    terminateProcess(target);
                    if (terminatingSelf) currentProcess = NULL;
                } else {
                    debug_print("[SYSCALL] TERMPROCESS: PID not found\n");
                    savedState->pc_epc += WORDLEN;
                    LDST(savedState);
                    break;
                }
            }

            scheduler();
            break;
        }

        /* ----------------------------------------------------------------
         * SYS3 - PASSEREN  (P)
         * a1 = int* semAddr
         * ---------------------------------------------------------------- */
        case PASSEREN: {
            debug_print("[SYSCALL] PASSEREN\n");
            int *semAddr = (int *) savedState->reg_a1;
            (*semAddr)--;
            savedState->pc_epc += WORDLEN;

            if (*semAddr < 0) {
                debug_print("[SYSCALL] Blocked on semaphore\n");
                updateCPUTime();
                copyState(&currentProcess->p_s, savedState);
                insertBlocked(semAddr, currentProcess);
                currentProcess = NULL;
                scheduler();
            } else {
                LDST(savedState);
            }
            break;
        }

        /* ----------------------------------------------------------------
         * SYS4 - VERHOGEN  (V)
         * a1 = int* semAddr
         * ---------------------------------------------------------------- */
        case VERHOGEN: {
            debug_print("[SYSCALL] VERHOGEN\n");
            int *semAddr = (int *) savedState->reg_a1;
            (*semAddr)++;
            if (*semAddr <= 0) {
                pcb_t *unblocked = removeBlocked(semAddr);
                if (unblocked) {
                    debug_hex("[SYSCALL] Unblocked PID=",
                              (unsigned int)unblocked->p_pid);
                    unblocked->p_semAdd = NULL;
                    insertProcQ(&readyQueue, unblocked);
                }
            }
            savedState->pc_epc += WORDLEN;
            LDST(savedState);
            break;
        }

        /* ----------------------------------------------------------------
         * SYS5 - DOIO
         * a1 = int* commandAddr  indirizzo registro comando del device
         * a2 = int  commandValue valore da scrivere
         *
         * Calcola il semaforo device dall'indirizzo, scrive il comando,
         * blocca il processo in attesa dell'interrupt di completamento.
         * ---------------------------------------------------------------- */
        case DOIO: {
            debug_print("[SYSCALL] DOIO\n");
            int *commandAddr  = (int *) savedState->reg_a1;
            int  commandValue = (int)   savedState->reg_a2;

            /*
             * Layout device registers:
             *   START_DEVREG + (line-3)*0x80 + dev*0x10 + subword*4
             * Terminali: word0=RX status, word1=RX cmd, word2=TX status, word3=TX cmd
             * Altri:     word0=status,    word1=command
             */
            unsigned int offset  = (unsigned int)commandAddr - START_DEVREG;
            int line    = (int)(offset / 0x80) + 3;
            int dev     = (int)((offset % 0x80) / 0x10);
            int subword = (int)((offset % 0x10) / WORDLEN);

            int semIdx;
            if (line == IL_TERMINAL)
                semIdx = (subword == 3) ? TERM_TX_SEM(dev) : TERM_RX_SEM(dev);
            else
                semIdx = DEV_SEM_BASE(line, dev);

            debug_hex("[DOIO] line=",   (unsigned int)line);
            debug_hex("[DOIO] dev=",    (unsigned int)dev);
            debug_hex("[DOIO] semIdx=", (unsigned int)semIdx);

            /* Scrivi il comando nel device register */
            *commandAddr = commandValue;

            /* Salva stato e blocca */
            savedState->pc_epc += WORDLEN;
            updateCPUTime();
            copyState(&currentProcess->p_s, savedState);

            devSems[semIdx]--;
            softBlockCount++;
            insertBlocked(&devSems[semIdx], currentProcess);
            currentProcess = NULL;
            scheduler();
            break;
        }

        /* ----------------------------------------------------------------
         * SYS6 - GETTIME
         * ret a0 = tempo CPU accumulato (cpu_t)
         * ---------------------------------------------------------------- */
        case GETTIME: {
            debug_print("[SYSCALL] GETTIME\n");
            updateCPUTime();
            savedState->reg_a0 = (unsigned int) currentProcess->p_time;
            savedState->pc_epc += WORDLEN;
            LDST(savedState);
            break;
        }

        /* ----------------------------------------------------------------
         * SYS7 - CLOCKWAIT
         * Blocca fino al prossimo tick pseudo-clock (100ms).
         * ---------------------------------------------------------------- */
        case CLOCKWAIT: {
            debug_print("[SYSCALL] CLOCKWAIT\n");
            savedState->pc_epc += WORDLEN;
            updateCPUTime();
            copyState(&currentProcess->p_s, savedState);

            devSems[PSEUDOCLK_SEM]--;
            softBlockCount++;
            insertBlocked(&devSems[PSEUDOCLK_SEM], currentProcess);
            currentProcess = NULL;
            scheduler();
            break;
        }

        /* ----------------------------------------------------------------
         * SYS8 - GETSUPPORTPTR
         * ret a0 = support_t* del processo corrente (NULL se non ha)
         * ---------------------------------------------------------------- */
        case GETSUPPORTPTR: {
            debug_print("[SYSCALL] GETSUPPORTPTR\n");
            savedState->reg_a0 = (unsigned int) currentProcess->p_supportStruct;
            savedState->pc_epc += WORDLEN;
            LDST(savedState);
            break;
        }

        /* ----------------------------------------------------------------
         * SYS9 - GETPROCESSID
         * a1 = 0 → PID del processo corrente
         * a1 = 1 → PID del padre (0 se radice)
         * ---------------------------------------------------------------- */
        case GETPROCESSID: {
            debug_print("[SYSCALL] GETPROCESSID\n");
            int wantParent = (int) savedState->reg_a1;
            if (wantParent == 0) {
                savedState->reg_a0 = (unsigned int) currentProcess->p_pid;
            } else {
                savedState->reg_a0 = currentProcess->p_parent
                    ? (unsigned int) currentProcess->p_parent->p_pid
                    : 0;
            }
            savedState->pc_epc += WORDLEN;
            LDST(savedState);
            break;
        }

        /* ----------------------------------------------------------------
         * SYS10 - YIELD
         * Reinserisce in ready queue e cede la CPU.
         * ---------------------------------------------------------------- */
        case YIELD: {
            debug_print("[SYSCALL] YIELD\n");
            savedState->pc_epc += WORDLEN;
            updateCPUTime();
            copyState(&currentProcess->p_s, savedState);
            insertProcQ(&readyQueue, currentProcess);
            currentProcess = NULL;
            scheduler();
            break;
        }

        default: {
            debug_print("[SYSCALL] Unknown -> passUpOrDie\n");
            passUpOrDie(GENERALEXCEPT);
            break;
        }
    }
}

/* -----------------------------------------------------------------------
 * passUpOrDie
 * Con support struct → passa al livello supporto.
 * Senza → termina il processo e tutto il suo sottoalbero.
 * ----------------------------------------------------------------------- */
static void passUpOrDie(int exceptionType) {
    debug_hex("[EXC] passUpOrDie type=", (unsigned int)exceptionType);

    if (!currentProcess || !currentProcess->p_supportStruct) {
        debug_print("[EXC] No support struct -> terminate\n");
        updateCPUTime();
        terminateProcess(currentProcess);
        currentProcess = NULL;
        scheduler();
    } else {
        debug_print("[EXC] Passing up\n");
        support_t *sup = currentProcess->p_supportStruct;
        copyState(&sup->sup_exceptState[exceptionType], (state_t *) BIOSDATAPAGE);
        context_t *ctx = &sup->sup_exceptContext[exceptionType];
        LDCXT(ctx->stackPtr, ctx->status, ctx->pc);
    }
}