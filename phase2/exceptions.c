/*
 * exceptions.c
 */

#include "../headers/const.h"
#include "../headers/types.h"
#include <uriscv/liburiscv.h>

#include "../phase1/headers/pcb.h"
#include "../phase1/headers/asl.h"
#include "./headers/globals.h"

/* Prototipi interni */
static void syscallHandler(state_t *savedState);
static void tlbExceptionHandler(void);
static void programTrapHandler(void);
static void passUpOrDie(int exceptionType);
static void terminateProcess(pcb_t *proc);
static void updateCPUTime(void);

extern void scheduler(void);
extern void interruptHandler(void);   /* <-- senza parametro */

static void copyState(state_t *dst, state_t *src) {
    unsigned int *d = (unsigned int *) dst;
    unsigned int *s = (unsigned int *) src;
    for (int i = 0; i < STATESIZE / WORDLEN; i++)
        d[i] = s[i];
}
#include "debug.c"
/* -----------------------------------------------------------------------
 * exceptionHandler
 * ----------------------------------------------------------------------- */
void exceptionHandler(void) {
    state_t *savedState = (state_t *) BIOSDATAPAGE;
    unsigned int cause   = savedState->cause;
    unsigned int excCode = (cause & GETEXECCODE) >> CAUSESHIFT;

    debug_hex("[EXC] cause=", cause);
    debug_hex("[EXC] excCode=", excCode);

    if (cause & 0x80000000) {
        interruptHandler();          /* mip viene letto dentro l'handler */
    } else if (excCode == 8 || excCode == 11) {
        debug_hex("[EXC] SYSCALL sysCode=", ((state_t*)BIOSDATAPAGE)->reg_a0);
        syscallHandler(savedState);
    } else if (excCode >= 24 && excCode <= 28) {
        tlbExceptionHandler();
    } else {
        programTrapHandler();
    }
}

/* -----------------------------------------------------------------------
 * updateCPUTime
 * ----------------------------------------------------------------------- */
static void updateCPUTime(void) {
    if (currentProcess != NULL) {
        cpu_t now;
        STCK(now);
        currentProcess->p_time += (now - startTOD);
        startTOD = now;
    }
}

/* -----------------------------------------------------------------------
 * terminateProcess
 * ----------------------------------------------------------------------- */
static void terminateProcess(pcb_t *proc) {
    if (proc == NULL) return;

    while (!emptyChild(proc)) {
        terminateProcess(removeChild(proc));
    }

    processCount--;

    if (proc == currentProcess) {
        currentProcess = NULL;
    } else if (proc->p_semAdd != NULL) {
        outBlocked(proc);
        for (int i = 0; i < TOT_SEMS; i++) {
            if (&devSems[i] == proc->p_semAdd) {
                softBlockCount--;
                break;
            }
        }
    } else {
        outProcQ(&readyQueue, proc);
    }

    outChild(proc);
    freePcb(proc);
}

/* -----------------------------------------------------------------------
 * syscallHandler
 * ----------------------------------------------------------------------- */
static void syscallHandler(state_t *savedState) {
    int sysCode = (int) savedState->reg_a0;

    if ((savedState->status & MSTATUS_MPP_MASK) == 0) {
        if (sysCode < 0) {
            savedState->cause = PRIVINSTR;
            programTrapHandler();
            return;
        }
    }

    if (sysCode >= 1) {
        passUpOrDie(GENERALEXCEPT);
        return;
    }

    switch (sysCode) {

        case CREATEPROCESS: {
            state_t   *newState   = (state_t *)   savedState->reg_a1;
            int        prio       = (int)          savedState->reg_a2;
            support_t *supportPtr = (support_t *)  savedState->reg_a3;

            pcb_t *child = allocPcb();
            if (child == NULL) {
                savedState->reg_a0 = (unsigned int) -1;
            } else {
                copyState(&child->p_s, newState);
                child->p_supportStruct = supportPtr;
                child->p_time         = 0;
                child->p_semAdd       = NULL;
                child->p_prio         = prio;
                insertProcQ(&readyQueue, child);
                insertChild(currentProcess, child);
                processCount++;
                savedState->reg_a0 = (unsigned int) child->p_pid;
            }
            savedState->pc_epc += WORDLEN;
            LDST(savedState);
            break;
        }

        case TERMPROCESS: {
            int targetPid = (int) savedState->reg_a1;

            if (targetPid == 0) {
                updateCPUTime();
                terminateProcess(currentProcess);
            } else {
                pcb_t *target = NULL;
                struct list_head *pos;
                list_for_each(pos, &readyQueue) {
                    pcb_t *p = container_of(pos, pcb_t, p_list);
                    if (p->p_pid == (unsigned int) targetPid) {
                        target = p;
                        break;
                    }
                }
                if (target == NULL) {
                    for (int i = 0; i < TOT_SEMS && target == NULL; i++) {
                        pcb_t *p = headBlocked(&devSems[i]);
                        while (p != NULL) {
                            if (p->p_pid == (unsigned int) targetPid) {
                                target = p;
                                break;
                            }
                            struct list_head *next = p->p_list.next;
                            if (next == &(((semd_t*)NULL)->s_procq)) break;
                            p = container_of(next, pcb_t, p_list);
                        }
                    }
                }
                if (target == NULL && currentProcess != NULL &&
                    currentProcess->p_pid == (unsigned int) targetPid) {
                    target = currentProcess;
                }
                if (target != NULL) {
                    if (target == currentProcess) updateCPUTime();
                    terminateProcess(target);
                }
            }
            scheduler();
            break;
        }

        case PASSEREN: {
            int *semAddr = (int *) savedState->reg_a1;
            savedState->pc_epc += WORDLEN;
            (*semAddr)--;
            if (*semAddr < 0) {
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

        case VERHOGEN: {
            int *semAddr = (int *) savedState->reg_a1;
            (*semAddr)++;
            if (*semAddr <= 0) {
                pcb_t *unblocked = removeBlocked(semAddr);
                if (unblocked != NULL) {
                    unblocked->p_semAdd = NULL;
                    insertProcQ(&readyQueue, unblocked);
                }
            }
            savedState->pc_epc += WORDLEN;
            LDST(savedState);
            break;
        }

        case DOIO: {
            int *commandAddr  = (int *) savedState->reg_a1;
            int  commandValue = (int)   savedState->reg_a2;

            int devOffset  = (int)commandAddr - START_DEVREG;
            int lineOffset = devOffset / 0x80;
            int withinLine = devOffset % 0x80;
            int devNo      = withinLine / 0x10;
            int withinDev  = withinLine % 0x10;
            int intLineNo  = lineOffset + 3;
            int semIdx;

            if (intLineNo == IL_TERMINAL) {
                semIdx = (withinDev == 0xC) ? TERM_TX_SEM(devNo) : TERM_RX_SEM(devNo);
            } else {
                semIdx = DEV_SEM_BASE(intLineNo, devNo);
            }

            savedState->pc_epc += WORDLEN;
            updateCPUTime();
            copyState(&currentProcess->p_s, savedState);
            devSems[semIdx]--;
            insertBlocked(&devSems[semIdx], currentProcess);
            softBlockCount++;
            currentProcess = NULL;
            *commandAddr = commandValue;
            scheduler();
            break;
        }

        case GETTIME: {
            cpu_t now;
            STCK(now);
            savedState->reg_a0 = currentProcess->p_time + (now - startTOD);
            savedState->pc_epc += WORDLEN;
            LDST(savedState);
            break;
        }

        case CLOCKWAIT: {
            savedState->pc_epc += WORDLEN;
            updateCPUTime();
            copyState(&currentProcess->p_s, savedState);
            devSems[PSEUDOCLK_SEM]--;
            insertBlocked(&devSems[PSEUDOCLK_SEM], currentProcess);
            softBlockCount++;
            currentProcess = NULL;
            scheduler();
            break;
        }

        case GETSUPPORTPTR: {
            savedState->reg_a0 = (unsigned int) currentProcess->p_supportStruct;
            savedState->pc_epc += WORDLEN;
            LDST(savedState);
            break;
        }

        case GETPROCESSID: {
            int parent = (int) savedState->reg_a1;
            if (parent == 0) {
                savedState->reg_a0 = (unsigned int) currentProcess->p_pid;
            } else {
                savedState->reg_a0 = (currentProcess->p_parent != NULL)
                    ? (unsigned int) currentProcess->p_parent->p_pid
                    : 0;
            }
            savedState->pc_epc += WORDLEN;
            LDST(savedState);
            break;
        }

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

static void tlbExceptionHandler(void) {
    passUpOrDie(PGFAULTEXCEPT);
}

static void programTrapHandler(void) {
    passUpOrDie(GENERALEXCEPT);
}

static void passUpOrDie(int exceptionType) {
    if (currentProcess->p_supportStruct == NULL) {
        updateCPUTime();
        terminateProcess(currentProcess);
        scheduler();
    } else {
        support_t *sup = currentProcess->p_supportStruct;
        copyState(&sup->sup_exceptState[exceptionType], (state_t *) BIOSDATAPAGE);
        context_t *ctx = &(sup->sup_exceptContext[exceptionType]);
        LDCXT(ctx->stackPtr, ctx->status, ctx->pc);
    }
}
