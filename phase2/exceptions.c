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

#ifndef CAUSE_EXCCODE_MASK
#define CAUSE_EXCCODE_MASK 0xFFu
#endif

/* klog - debug buffer visibile nel debugger uRISCV */
extern void klog_print(char *str);
extern void klog_print_hex(unsigned int num);

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
 * Funziona per qualsiasi processo vivo: running, ready o blocked.
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

    /* 1. Termina ricorsivamente i figli */
    pcb_t *child;
    while ((child = removeChild(proc)) != NULL)
        terminateProcess(child);

    /* 2. Gestione Semafori */
    if (proc->p_semAdd != NULL) {
        int *sem = proc->p_semAdd;
        outBlocked(proc);
        proc->p_semAdd = NULL;

        if (isDeviceSemaphore(sem)) {
            softBlockCount--;
        } else {
            (*sem)++;
        }
    } else if (proc != currentProcess) {
        outProcQ(&readyQueue, proc);
    }

    /* 3. Pulizia finale */
    activeProcs_remove(proc);
    outChild(proc);

    if (proc == currentProcess)
        currentProcess = NULL;

    klog_print("TERM pid="); klog_print_hex((unsigned int)proc->p_pid);
    klog_print(" pc=");      klog_print_hex((unsigned int)processCount);

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

    state_t *savedState = (state_t *) BIOSDATAPAGE;

    /* CPU time accounting */
    cpu_t now;
    STCK(now);
    if (currentProcess != NULL) {
        currentProcess->p_time += (now - startTOD);
    }
    startTOD = now;

    unsigned int cause   = savedState->cause;
    unsigned int excCode = cause & CAUSE_EXCCODE_MASK;

    if (cause & 0x80000000) {
        /* Interrupt: interruptHandler() deve chiamare scheduler() o LDST() */
        interruptHandler();
        /* NON deve arrivare qui: interruptHandler non ritorna */
        PANIC();
    }
    else if (excCode == 8 || excCode == 11) {
        /* ECALL da U-mode (8) o M-mode (11) */
        syscallHandler(savedState);
        /* syscallHandler gestisce sempre il flusso con LDST/scheduler: non ritorna */
        PANIC();
    }
    else if (excCode == 12 || excCode == 13 || excCode == 15) {
        /* Page fault genuini → TLB handler */
        tlbExceptionHandler();
    }
    else if (excCode == 1 || excCode == 5 || excCode == 7) {
        unsigned int mpp = savedState->status & MSTATUS_MPP_MASK;
        if (mpp == 0) {
            /* user mode access fault → GENERALEXCEPT */
            savedState->cause = 5;
            programTrapHandler();
        } else {
            /* kernel mode → PGFAULTEXCEPT */
            tlbExceptionHandler();
        }
    }
    else {
        programTrapHandler();
    }

    /*
     * FIX: questo punto è raggiunto solo da tlbExceptionHandler/programTrapHandler
     * quando passUpOrDie fa LDCXT (e quindi non ritorna) oppure quando il processo
     * non ha support struct e viene terminato. In quest'ultimo caso passUpOrDie
     * chiama già scheduler() che non ritorna, quindi non dovremmo mai arrivare qui.
     * Aggiungiamo PANIC() come safety net.
     */
    PANIC();
}

/* -----------------------------------------------------------------------
 * syscallHandler
 * ----------------------------------------------------------------------- */
static void syscallHandler(state_t *savedState) {
    int sysCode = (int) savedState->reg_a0;

    /* Syscall negativa da user mode → program trap (PRIVINSTR) */
    if ((savedState->status & MSTATUS_MPP_MASK) == 0 && sysCode < 0) {
        savedState->cause = PRIVINSTR;
        programTrapHandler();
        return;
    }

    /*
     * FIX: sysCode >= 1 non gestita dal kernel → passUpOrDie(GENERALEXCEPT).
     * Bisogna avanzare PC *prima* di passare al support handler, altrimenti
     * se il support handler ritorna (o se non c'è e il processo viene terminato
     * e poi ricreato) si riesegue la stessa ecall in loop.
     */
    if (sysCode >= 1) {
        savedState->pc_epc += WORDLEN;
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
            state_t   *newState = (state_t *)  savedState->reg_a1;
            int        prio     = (int)         savedState->reg_a2;
            support_t *support  = (support_t *) savedState->reg_a3;

            pcb_t *child = allocPcb();
            if (!child) {
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

                savedState->reg_a0 = (unsigned int) child->p_pid;
            }

            savedState->pc_epc += WORDLEN;
            copyState(&currentProcess->p_s, savedState);
            LDST(&currentProcess->p_s);
            break;
        }

        /* ----------------------------------------------------------------
         * SYS2 - TERMPROCESS
         * a1 = pid  (0 = processo corrente)
         * ---------------------------------------------------------------- */
        case TERMPROCESS: {
            int targetPid = (int) savedState->reg_a1;
            klog_print("SYS2 caller="); klog_print_hex(currentProcess->p_pid);
            klog_print(" target=");     klog_print_hex((unsigned int)targetPid);
            klog_print(" pc=");         klog_print_hex((unsigned int)processCount);
            updateCPUTime();

            if (targetPid == 0) {
                terminateProcess(currentProcess);
                currentProcess = NULL;
            } else {
                pcb_t *target = findProcessByPid(targetPid);
                if (target) {
                    if (target == currentProcess) {
                        terminateProcess(target);
                        currentProcess = NULL;
                    } else {
                        terminateProcess(target);
                    }
                } else {
                    /* PID non trovato: ritorna al chiamante senza fare nulla */
                    savedState->pc_epc += WORDLEN;
                    LDST(savedState);
                    /* LDST non ritorna */
                }
            }

            if (currentProcess == NULL) {
                scheduler();
            } else {
                savedState->pc_epc += WORDLEN;
                LDST(savedState);
            }
            break;
        }

        /* ----------------------------------------------------------------
         * SYS3 - PASSEREN  (P)
         * a1 = int* semAddr
         * ---------------------------------------------------------------- */
        case PASSEREN: {
            int *semAddr = (int *) savedState->reg_a1;
            (*semAddr)--;
            savedState->pc_epc += WORDLEN;

            if (*semAddr < 0) {
                updateCPUTime();
                copyState(&currentProcess->p_s, savedState);
                currentProcess->p_semAdd = semAddr;
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
            int *semAddr = (int *) savedState->reg_a1;
            (*semAddr)++;

            if (*semAddr <= 0) {
                pcb_t *p = removeBlocked(semAddr);
                /* FIX: removeBlocked può tornare NULL se la coda è inconsistente */
                if (p != NULL) {
                    p->p_semAdd = NULL;
                    insertProcQ(&readyQueue, p);
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
         * ---------------------------------------------------------------- */
        case DOIO: {
            int *commandAddr  = (int *) savedState->reg_a1;
            int  commandValue = (int)   savedState->reg_a2;

            unsigned int offset  = (unsigned int)commandAddr - START_DEVREG;
            int line    = (int)(offset / 0x80) + 3;
            int dev     = (int)((offset % 0x80) / 0x10);
            int subword = (int)((offset % 0x10) / WORDLEN);

            int semIdx;
            if (line == 7)  /* IntlineNo for IL_TERMINAL */
                semIdx = (subword == 3) ? TERM_TX_SEM(dev) : TERM_RX_SEM(dev);
            else
                semIdx = DEV_SEM_BASE(line, dev);

            /* Scrivi il comando nel device register */
            *commandAddr = commandValue;

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
            updateCPUTime();
            savedState->reg_a0 = (unsigned int) currentProcess->p_time;
            savedState->pc_epc += WORDLEN;
            LDST(savedState);
            break;
        }

        /* ----------------------------------------------------------------
         * SYS7 - CLOCKWAIT
         * ---------------------------------------------------------------- */
        case CLOCKWAIT: {
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
         * ret a0 = support_t* del processo corrente
         * ---------------------------------------------------------------- */
        case GETSUPPORTPTR: {
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
         * ---------------------------------------------------------------- */
        case YIELD: {
            savedState->pc_epc += WORDLEN;
            updateCPUTime();
            copyState(&currentProcess->p_s, savedState);
            insertProcQ(&readyQueue, currentProcess);
            currentProcess = NULL;
            scheduler();
            break;
        }

        default: {
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
    if (!currentProcess || !currentProcess->p_supportStruct) {
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