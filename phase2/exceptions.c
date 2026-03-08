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
#include "../headers/listx.h"

#ifndef CAUSE_EXCCODE_MASK
#define CAUSE_EXCCODE_MASK 0xFFu
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

static void copyState(state_t *dst, state_t *src) {
    unsigned int *d = (unsigned int *) dst;
    unsigned int *s = (unsigned int *) src;
    for (int i = 0; i < STATE_T_SIZE_IN_BYTES / WORDLEN; i++)
        d[i] = s[i];
}

static void updateCPUTime(void) {
    if (currentProcess) {
        cpu_t now;
        STCK(now);
        currentProcess->p_time += now - startTOD;
        startTOD = now;
    }
}

/* TRUE solo per i semafori di device, NON per il pseudo‑clock */
static int isDeviceSemaphore(int *semAdd) {
    int *base = &devSems[0];
    int *top  = &devSems[TOT_SEMS];

    if (semAdd < base || semAdd >= top) return 0;

    int idx = (int)(semAdd - base);
    if (idx == PSEUDOCLK_SEM) return 0;   /* pseudo‑clock NON è device */

    return 1;
}

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

    insertBlocked(sem, currentProcess);
    currentProcess = NULL;
    scheduler();
}

static void activeProcs_add(pcb_t *p) {
    for (int i = 0; i < MAXPROC; i++) {
        if (activeProcs[i] == NULL) {
            activeProcs[i] = p;
            return;
        }
    }
    PANIC();
}

static void activeProcs_remove(pcb_t *p) {
    for (int i = 0; i < MAXPROC; i++) {
        if (activeProcs[i] == p) {
            activeProcs[i] = NULL;
            return;
        }
    }
}

static pcb_t *findProcessByPid(int target) {
    for (int i = 0; i < MAXPROC; i++) {
        if (activeProcs[i] != NULL && activeProcs[i]->p_pid == target)
            return activeProcs[i];
    }
    return NULL;
}

static void terminateProcess(pcb_t *p) {
    if (p == NULL) return;

    /* Evita doppi terminate */
    if (p->p_pid == -1) return;

    /* Marca come terminato */
    int oldPid = p->p_pid;
    p->p_pid = -1;

    /* Termina ricorsivamente i figli */
    pcb_t *child;
    while ((child = removeChild(p)) != NULL) {
        terminateProcess(child);
    }

    /* Rimuovi da activeProcs */
    activeProcs_remove(p);

    /* Se era bloccato su un semaforo */
    if (p->p_semAdd != NULL) {
        int *sem = p->p_semAdd;

        pcb_t *removed = outBlocked(p);
        p->p_semAdd = NULL;

        if (removed != NULL) {
            if (isDeviceSemaphore(sem)) {
                softBlockCount--;
            } else {
                (*sem)++;
            }
        }
    }

    /* Se era nella ready queue */
    outProcQ(&readyQueue, p);

    /* Se era il processo corrente */
    if (p == currentProcess) {
        currentProcess = NULL;
    }

    freePcb(p);
    processCount--;

}


static void tlbExceptionHandler(void) {
    passUpOrDie(PGFAULTEXCEPT);
}

static void programTrapHandler(void) {
    passUpOrDie(GENERALEXCEPT);
}

/* entry point principale */
void exceptionHandler(void) {
    state_t *savedState = (state_t *) BIOSDATAPAGE;
    unsigned int cause   = savedState->cause;
    unsigned int excCode = cause & CAUSE_EXCCODE_MASK;

    updateCPUTime();

    /* INTERRUPT */
    if (cause & 0x80000000) {
        interruptHandler();
        return;   // NON PANIC
    }
    /* SYS/BREAKPOINT */
    else if (excCode == 8 || excCode == 11) {
        savedState->pc_epc += WORDLEN;
        syscallHandler(savedState);
        return;   // NON PANIC
    }
    /* TLB */
    else if (excCode == 12 || excCode == 13 || excCode == 15) {
        tlbExceptionHandler();
        return;
    }
    /* ADDRESS/BUS ERROR */
    else if (excCode == 1 || excCode == 5 || excCode == 7) {
        unsigned int mpp = savedState->status & MSTATUS_MPP_MASK;
        if (mpp == 0) {
            savedState->cause = 5;
            programTrapHandler();
        } else {
            tlbExceptionHandler();
        }
        return;
    }
    /* altro → program trap */
    else {
        programTrapHandler();
        return;
    }
}



/* syscall handler: pc_epc già avanzato in exceptionHandler */
static void syscallHandler(state_t *savedState) {
    int sysCode = (int) savedState->reg_a0;

    if ((savedState->status & MSTATUS_MPP_MASK) == 0 && sysCode < 0) {
        savedState->cause = PRIVINSTR;
        programTrapHandler();
        return;
    }

    if (sysCode >= 1) {
        passUpOrDie(GENERALEXCEPT);
        return;
    }

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

            copyState(&child->p_s, newState);
            child->p_supportStruct = support;
            child->p_time          = 0;
            child->p_semAdd        = NULL;
            child->p_prio          = prio;

            activeProcs_add(child);
            insertChild(currentProcess, child);
            processCount++;

            savedState->reg_a0 = (unsigned int) child->p_pid;
            copyState(&currentProcess->p_s, savedState);

            if (child->p_prio > currentProcess->p_prio) {
                insertProcQ(&readyQueue, currentProcess);
                insertProcQ(&readyQueue, child);
                currentProcess = NULL;
                scheduler();
            } else {
                insertProcQ(&readyQueue, child);
                LDST(&currentProcess->p_s);
            }
            break;
        }

        case TERMPROCESS: {
            int targetPid = (int) savedState->reg_a1;
            updateCPUTime();

            pcb_t *target = (targetPid == 0) ? currentProcess : findProcessByPid(targetPid);
            if (!target) {
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

            if (*semAddr < 0) {
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

            if (*semAddr <= 0) {
                pcb_t *unblocked = removeBlocked(semAddr);
                if (unblocked) {
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

            *commandAddr = commandValue;

            updateCPUTime();
            copyState(&currentProcess->p_s, savedState);

            unsigned int *devBase = (unsigned int *)(START_DEVREG + (line - 3)*0x80 + dev*0x10);
            int ready = 0;

            if (line == 7) {
                if (subword == 3) {
                    if (!(devBase[3] & 0x1)) {
                        devSems[semIdx]--;
                        blockCurrentProcess(&devSems[semIdx]);
                    } else {
                        unsigned int status = devBase[2];   // TRANSM_STATUS
                        *commandAddr = commandValue;
                        devBase[3] = 1;                     // ACK
                        savedState->reg_a0 = status;        // <-- RESTITUISCI LO STATUS
                        LDST(&currentProcess->p_s);         // <-- TORNA AL CHIAMANTE
                    }

                } else {
                    if (!(devBase[1] & 0x1)) {
                        devSems[semIdx]--;
                        blockCurrentProcess(&devSems[semIdx]);
                    } else {
                        unsigned int status = devBase[0];   // RECV_STATUS
                        devBase[1] = 1;                     // ACK
                        savedState->reg_a0 = status;
                        LDST(&currentProcess->p_s);
                    }
                }
            } else {
                ready = devBase[1] & 0x1;
                if (!ready) {
                    devSems[semIdx]--;
                    blockCurrentProcess(&devSems[semIdx]);
                }
            }

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
            softBlockCount++;                     /* solo qui */
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
            insertProcQ(&readyQueue, currentProcess);
            currentProcess = NULL;
            scheduler();
            break;
        }

        default:
            passUpOrDie(GENERALEXCEPT);
    }
}

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
