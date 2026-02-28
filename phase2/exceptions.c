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
/* Add these implementations to exceptions.c, before exceptionHandler() */

static void updateCPUTime(void) {
    if (currentProcess) {
        cpu_t now;
        STCK(now);
        currentProcess->p_time += now - startTOD;
        STCK(startTOD);
    }
}

static void terminateProcess(pcb_t *proc) {
    if (!proc) return;

    /* Recursively terminate all children */
    pcb_t *child;
    while ((child = removeChild(proc)) != NULL)
        terminateProcess(child);

    /* If blocked on a semaphore, remove from ASL */
    if (proc->p_semAdd != NULL) {
        outBlocked(proc);
        /* If it's a device semaphore, don't decrement softBlockCount here
           — adjust logic based on your semaphore convention */
        softBlockCount--;
    } else {
        /* Remove from ready queue if it's there and not currentProcess */
        outProcQ(&readyQueue, proc);
    }

    /* Detach from parent */
    outChild(proc);
    freePcb(proc);
    processCount--;
}

static void tlbExceptionHandler(void) {
    passUpOrDie(PGFAULTEXCEPT);
}

static void programTrapHandler(void) {
    passUpOrDie(GENERALEXCEPT);
}
#include "debug.h"
/* -----------------------------------------------------------------------
 * exceptionHandler
 * ----------------------------------------------------------------------- */
void exceptionHandler(void) {
    state_t *savedState = (state_t *) BIOSDATAPAGE;
    unsigned int cause   = savedState->cause;
    unsigned int excCode = (cause & GETEXECCODE) >> CAUSESHIFT;

    debug_hex("[EXC] cause=", cause);
    debug_hex("[EXC] excCode=", excCode);
    debug_hex("[EXC] currentProcess=", (unsigned int)currentProcess);

    if (cause & 0x80000000) {
        debug_print("[EXC] Interrupt detected, calling interruptHandler()\n");
        interruptHandler();
    } 
    else if (excCode == 8 || excCode == 11) {
        debug_hex("[SYSCALL] sysCode=", savedState->reg_a0);
        syscallHandler(savedState);
    } 
    else if (excCode >= 24 && excCode <= 28) {
        debug_print("[EXC] TLB Exception\n");
        tlbExceptionHandler();
    } 
    else {
        debug_print("[EXC] Program Trap\n");
        programTrapHandler();
    }
}

/* -----------------------------------------------------------------------
 * syscallHandler
 * ----------------------------------------------------------------------- */
static void syscallHandler(state_t *savedState) {
    int sysCode = (int) savedState->reg_a0;
    debug_hex("[SYSCALL] Invoked sysCode=", sysCode);
    debug_hex("[SYSCALL] currentProcess PID=", currentProcess ? currentProcess->p_pid : 0);

    if ((savedState->status & MSTATUS_MPP_MASK) == 0 && sysCode < 0) {
        debug_print("[SYSCALL] Privileged instruction in user mode\n");
        savedState->cause = PRIVINSTR;
        programTrapHandler();
        return;
    }

    if (sysCode >= 1) {
        debug_print("[SYSCALL] General exception for unimplemented syscall\n");
        passUpOrDie(GENERALEXCEPT);
        return;
    }

    switch (sysCode) {
        case CREATEPROCESS: {
            debug_print("[SYSCALL] CREATEPROCESS\n");
            state_t *newState   = (state_t *) savedState->reg_a1;
            int prio            = (int) savedState->reg_a2;
            support_t *support  = (support_t *) savedState->reg_a3;

            pcb_t *child = allocPcb();
            if (!child) {
                debug_print("[SYSCALL] CREATEPROCESS failed: no PCB\n");
                savedState->reg_a0 = (unsigned int) -1;
            } else {
                copyState(&child->p_s, newState);
                child->p_supportStruct = support;
                child->p_time         = 0;
                child->p_semAdd       = NULL;
                child->p_prio         = prio;

                insertProcQ(&readyQueue, child);
                insertChild(currentProcess, child);
                processCount++;

                debug_hex("[SYSCALL] Created child PID=", child->p_pid);
                savedState->reg_a0 = child->p_pid;
            }

            savedState->pc_epc += WORDLEN;
            LDST(savedState);
            break;
        }

        case TERMPROCESS: {
            debug_print("[SYSCALL] TERMPROCESS\n");
            int targetPid = (int) savedState->reg_a1;
            debug_hex("[SYSCALL] targetPid=", targetPid);
            updateCPUTime();
            terminateProcess(currentProcess);  // semplificato per esempio
            scheduler();
            break;
        }

        case PASSEREN: {
            debug_print("[SYSCALL] PASSEREN\n");
            int *semAddr = (int *) savedState->reg_a1;
            (*semAddr)--;
            savedState->pc_epc += WORDLEN;

            if (*semAddr < 0) {
                debug_print("[SYSCALL] Process blocked on semaphore\n");
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
            debug_print("[SYSCALL] VERHOGEN\n");
            int *semAddr = (int *) savedState->reg_a1;
            (*semAddr)++;
            if (*semAddr <= 0) {
                pcb_t *unblocked = removeBlocked(semAddr);
                if (unblocked) {
                    debug_hex("[SYSCALL] Unblocking process PID=", unblocked->p_pid);
                    unblocked->p_semAdd = NULL;
                    insertProcQ(&readyQueue, unblocked);
                }
            }
            savedState->pc_epc += WORDLEN;
            LDST(savedState);
            break;
        }

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
            debug_print("[SYSCALL] Unknown or unimplemented syscall -> passUpOrDie\n");
            passUpOrDie(GENERALEXCEPT);
            break;
        }
    }
}

/* -----------------------------------------------------------------------
 * passUpOrDie
 * ----------------------------------------------------------------------- */
static void passUpOrDie(int exceptionType) {
    debug_hex("[EXC] passUpOrDie exceptionType=", exceptionType);
    if (!currentProcess->p_supportStruct) {
        debug_print("[EXC] No support structure -> terminate process\n");
        updateCPUTime();
        terminateProcess(currentProcess);
        scheduler();
    } else {
        debug_print("[EXC] Using support structure, copying state to sup_exceptState\n");
        support_t *sup = currentProcess->p_supportStruct;
        copyState(&sup->sup_exceptState[exceptionType], (state_t *) BIOSDATAPAGE);
        context_t *ctx = &(sup->sup_exceptContext[exceptionType]);
        LDCXT(ctx->stackPtr, ctx->status, ctx->pc);
    }
}
