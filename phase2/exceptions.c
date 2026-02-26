/*
 * exceptions.c
 * Gestisce tutte le eccezioni del Nucleo (esclusi gli interrupt,
 * gestiti in interrupts.c):
 *
 *  - uTLB_RefillHandler : handler TLB-Refill (placeholder Phase 2)
 *  - exceptionHandler   : entry point per tutte le eccezioni non TLB-Refill
 *  - syscallHandler     : gestisce NSYS1..NSYS10
 *  - tlbExceptionHandler: Pass Up or Die con indice PGFAULTEXCEPT
 *  - programTrapHandler : Pass Up or Die con indice GENERALEXCEPT
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

/* Dichiarazione scheduler (definito in scheduler.c) */
extern void scheduler(void);
/* Dichiarazione interrupt handler (definito in interrupts.c) */
extern void interruptHandler(void);

/* -----------------------------------------------------------------------
 * Debug utilities
 * ----------------------------------------------------------------------- */
static void debug_print(const char *msg) {
    unsigned int *command = (unsigned int *)(0x10000254 + 3*4);
    while (*msg != '\0') {
        *command = 2 | (((unsigned int)*msg) << 8);
        for (volatile int i = 0; i < 10000; i++);
        msg++;
    }
}

static void debug_hex(const char *label, unsigned int val) {
    unsigned int *command = (unsigned int *)(0x10000254 + 3*4);
    const char *p = label;
    while (*p) {
        *command = 2 | (((unsigned int)*p) << 8);
        for (volatile int i = 0; i < 10000; i++);
        p++;
    }
    char hex[9];
    for (int i = 7; i >= 0; i--) {
        int nibble = val & 0xF;
        hex[i] = nibble < 10 ? '0' + nibble : 'a' + nibble - 10;
        val >>= 4;
    }
    hex[8] = '\0';
    for (int i = 0; i < 8; i++) {
        *command = 2 | (((unsigned int)hex[i]) << 8);
        for (volatile int i = 0; i < 10000; i++);
    }
    *command = 2 | (((unsigned int)'\n') << 8);
    for (volatile int i = 0; i < 10000; i++);
}

/*
 * copyState - copia uno state_t word per word.
 */
static void copyState(state_t *dst, state_t *src) {
    unsigned int *d = (unsigned int *) dst;
    unsigned int *s = (unsigned int *) src;
    for (int i = 0; i < STATESIZE / WORDLEN; i++) {
        d[i] = s[i];
    }
}


/* -----------------------------------------------------------------------
 * exceptionHandler
 * ----------------------------------------------------------------------- */
void exceptionHandler(void) {
    debug_print("[EXC] exceptionHandler entered\n");

    state_t *savedState = (state_t *) BIOSDATAPAGE;

    unsigned int cause   = savedState->cause;
    unsigned int excCode = (cause & GETEXECCODE) >> CAUSESHIFT;

    debug_hex("[EXC] cause=", cause);
    debug_hex("[EXC] excCode=", excCode);
    debug_hex("[EXC] pc_epc=", (unsigned int)savedState->pc_epc);

    if (cause & 0x80000000) {
        debug_print("[EXC] -> INTERRUPT\n");
        interruptHandler();
    } else if (excCode == 8 || excCode == 11) {
        debug_print("[EXC] -> SYSCALL\n");
        syscallHandler(savedState);
    } else if (excCode >= 24 && excCode <= 28) {
        debug_print("[EXC] -> TLB EXCEPTION\n");
        tlbExceptionHandler();
    } else {
        debug_print("[EXC] -> PROGRAM TRAP\n");
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
        debug_hex("[CPU] updated p_time=", (unsigned int)currentProcess->p_time);
    }
}


/* -----------------------------------------------------------------------
 * terminateProcess
 * ----------------------------------------------------------------------- */
static void terminateProcess(pcb_t *proc) {
    if (proc == NULL) {
        debug_print("[TERM] terminateProcess called with NULL\n");
        return;
    }

    debug_hex("[TERM] terminating pid=", (unsigned int)proc->p_pid);

    while (!emptyChild(proc)) {
        terminateProcess(removeChild(proc));
    }

    processCount--;
    debug_hex("[TERM] processCount now=", (unsigned int)processCount);

    if (proc == currentProcess) {
        debug_print("[TERM] was currentProcess -> set to NULL\n");
        currentProcess = NULL;
    } else if (proc->p_semAdd != NULL) {
        debug_hex("[TERM] blocked on semAdd=", (unsigned int)proc->p_semAdd);
        outBlocked(proc);

        for (int i = 0; i < TOT_SEMS; i++) {
            if (&devSems[i] == proc->p_semAdd) {
                softBlockCount--;
                debug_hex("[TERM] softBlockCount now=", (unsigned int)softBlockCount);
                break;
            }
        }
    } else {
        debug_print("[TERM] was in readyQueue -> removing\n");
        outProcQ(&readyQueue, proc);
    }

    outChild(proc);
    freePcb(proc);
    debug_print("[TERM] done\n");
}


/* -----------------------------------------------------------------------
 * syscallHandler
 * ----------------------------------------------------------------------- */
static void syscallHandler(state_t *savedState) {
    int sysCode = (int) savedState->reg_a0;

    debug_hex("[SYS] sysCode=", (unsigned int)sysCode);
    debug_hex("[SYS] status=", (unsigned int)savedState->status);

    if ((savedState->status & MSTATUS_MPP_MASK) == 0) {
        debug_print("[SYS] user-mode process\n");
        if (sysCode < 0) {
            debug_print("[SYS] privileged syscall in user-mode -> PRIVINSTR trap\n");
            savedState->cause = PRIVINSTR;
            programTrapHandler();
            return;
        }
    }

    if (sysCode >= 1) {
        debug_print("[SYS] positive sysCode -> passUpOrDie(GENERALEXCEPT)\n");
        passUpOrDie(GENERALEXCEPT);
        return;
    }

    switch (sysCode) {

        /* NSYS1 - CREATEPROCESS */
        case CREATEPROCESS: {
            debug_print("[SYS] CREATEPROCESS\n");
            state_t   *newState   = (state_t *)   savedState->reg_a1;
            int        prio       = (int)          savedState->reg_a2;
            support_t *supportPtr = (support_t *)  savedState->reg_a3;

            debug_hex("[SYS] CREATEPROCESS prio=", (unsigned int)prio);

            pcb_t *child = allocPcb();
            if (child == NULL) {
                debug_print("[SYS] CREATEPROCESS failed: no free PCBs\n");
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
                debug_hex("[SYS] CREATEPROCESS new pid=", (unsigned int)child->p_pid);
                debug_hex("[SYS] processCount now=", (unsigned int)processCount);
            }

            savedState->pc_epc += WORDLEN;
            LDST(savedState);
            break;
        }

        /* NSYS2 - TERMPROCESS */
        case TERMPROCESS: {
            int targetPid = (int) savedState->reg_a1;
            debug_hex("[SYS] TERMPROCESS targetPid=", (unsigned int)targetPid);

            if (targetPid == 0) {
                debug_print("[SYS] TERMPROCESS self\n");
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
                    debug_print("[SYS] TERMPROCESS: not in readyQueue, searching devSems\n");
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
                    debug_print("[SYS] TERMPROCESS: target is currentProcess\n");
                    target = currentProcess;
                }

                if (target != NULL) {
                    debug_hex("[SYS] TERMPROCESS: found target pid=", (unsigned int)target->p_pid);
                    if (target == currentProcess) {
                        updateCPUTime();
                    }
                    terminateProcess(target);
                } else {
                    debug_print("[SYS] TERMPROCESS: target not found (already terminated?)\n");
                }
            }

            scheduler();
            break;
        }

        /* NSYS3 - PASSEREN */
        case PASSEREN: {
            int *semAddr = (int *) savedState->reg_a1;
            debug_hex("[SYS] PASSEREN semAddr=", (unsigned int)semAddr);

            savedState->pc_epc += WORDLEN;
            (*semAddr)--;
            debug_hex("[SYS] PASSEREN semVal after P=", (unsigned int)*semAddr);

            if (*semAddr < 0) {
                debug_print("[SYS] PASSEREN blocking process\n");
                updateCPUTime();
                copyState(&currentProcess->p_s, savedState);
                insertBlocked(semAddr, currentProcess);
                currentProcess = NULL;
                scheduler();
            } else {
                debug_print("[SYS] PASSEREN non-blocking, returning\n");
                LDST(savedState);
            }
            break;
        }

        /* NSYS4 - VERHOGEN */
        case VERHOGEN: {
            int *semAddr = (int *) savedState->reg_a1;
            debug_hex("[SYS] VERHOGEN semAddr=", (unsigned int)semAddr);

            (*semAddr)++;
            debug_hex("[SYS] VERHOGEN semVal after V=", (unsigned int)*semAddr);

            if (*semAddr <= 0) {
                pcb_t *unblocked = removeBlocked(semAddr);
                if (unblocked != NULL) {
                    debug_hex("[SYS] VERHOGEN unblocked pid=", (unsigned int)unblocked->p_pid);
                    unblocked->p_semAdd = NULL;
                    insertProcQ(&readyQueue, unblocked);
                } else {
                    debug_print("[SYS] VERHOGEN: semVal<=0 but no blocked process\n");
                }
            }

            savedState->pc_epc += WORDLEN;
            LDST(savedState);
            break;
        }

        /* NSYS5 - DOIO */
        case DOIO: {
            int *commandAddr  = (int *) savedState->reg_a1;
            int  commandValue = (int)   savedState->reg_a2;

            debug_hex("[SYS] DOIO commandAddr=", (unsigned int)commandAddr);
            debug_hex("[SYS] DOIO commandValue=", (unsigned int)commandValue);

            int devOffset = (int)commandAddr - START_DEVREG;
            int lineOffset, devNo, semIdx;

            lineOffset = devOffset / 0x80;
            int withinLine = devOffset % 0x80;
            devNo = withinLine / 0x10;
            int withinDev = withinLine % 0x10;

            int intLineNo = lineOffset + 3;

            debug_hex("[SYS] DOIO intLineNo=", (unsigned int)intLineNo);
            debug_hex("[SYS] DOIO devNo=", (unsigned int)devNo);
            debug_hex("[SYS] DOIO withinDev=", (unsigned int)withinDev);

            if (intLineNo == IL_TERMINAL) {
                if (withinDev == 0xC) {
                    semIdx = TERM_TX_SEM(devNo);
                    debug_print("[SYS] DOIO terminal TX\n");
                } else {
                    semIdx = TERM_RX_SEM(devNo);
                    debug_print("[SYS] DOIO terminal RX\n");
                }
            } else {
                semIdx = DEV_SEM_BASE(intLineNo, devNo);
            }

            debug_hex("[SYS] DOIO semIdx=", (unsigned int)semIdx);

            savedState->pc_epc += WORDLEN;
            updateCPUTime();
            copyState(&currentProcess->p_s, savedState);

            devSems[semIdx]--;
            debug_hex("[SYS] DOIO devSems[semIdx] after P=", (unsigned int)devSems[semIdx]);

            insertBlocked(&devSems[semIdx], currentProcess);
            softBlockCount++;
            debug_hex("[SYS] DOIO softBlockCount now=", (unsigned int)softBlockCount);
            currentProcess = NULL;

            *commandAddr = commandValue;
            debug_print("[SYS] DOIO command written, calling scheduler\n");

            scheduler();
            break;
        }

        /* NSYS6 - GETCPUTIME */
        case GETTIME: {
            debug_print("[SYS] GETTIME\n");
            cpu_t now;
            STCK(now);
            savedState->reg_a0 = currentProcess->p_time + (now - startTOD);
            debug_hex("[SYS] GETTIME result=", (unsigned int)savedState->reg_a0);

            savedState->pc_epc += WORDLEN;
            LDST(savedState);
            break;
        }

        /* NSYS7 - WAITCLOCK */
        case CLOCKWAIT: {
            debug_print("[SYS] CLOCKWAIT: blocking on pseudo-clock\n");

            savedState->pc_epc += WORDLEN;
            updateCPUTime();
            copyState(&currentProcess->p_s, savedState);

            devSems[PSEUDOCLK_SEM]--;
            debug_hex("[SYS] CLOCKWAIT devSems[PSEUDOCLK_SEM]=", (unsigned int)devSems[PSEUDOCLK_SEM]);

            insertBlocked(&devSems[PSEUDOCLK_SEM], currentProcess);
            softBlockCount++;
            debug_hex("[SYS] CLOCKWAIT softBlockCount now=", (unsigned int)softBlockCount);
            currentProcess = NULL;

            scheduler();
            break;
        }

        /* NSYS8 - GETSUPPORTPTR */
        case GETSUPPORTPTR: {
            debug_print("[SYS] GETSUPPORTPTR\n");
            savedState->reg_a0 = (unsigned int) currentProcess->p_supportStruct;
            debug_hex("[SYS] GETSUPPORTPTR result=", (unsigned int)savedState->reg_a0);

            savedState->pc_epc += WORDLEN;
            LDST(savedState);
            break;
        }

        /* NSYS9 - GETPROCESSID */
        case GETPROCESSID: {
            int parent = (int) savedState->reg_a1;
            debug_hex("[SYS] GETPROCESSID parent flag=", (unsigned int)parent);

            if (parent == 0) {
                savedState->reg_a0 = (unsigned int) currentProcess->p_pid;
                debug_hex("[SYS] GETPROCESSID own pid=", (unsigned int)savedState->reg_a0);
            } else {
                if (currentProcess->p_parent != NULL) {
                    savedState->reg_a0 = (unsigned int) currentProcess->p_parent->p_pid;
                } else {
                    savedState->reg_a0 = 0;
                }
                debug_hex("[SYS] GETPROCESSID parent pid=", (unsigned int)savedState->reg_a0);
            }

            savedState->pc_epc += WORDLEN;
            LDST(savedState);
            break;
        }

        /* NSYS10 - YIELD */
        case YIELD: {
            debug_print("[SYS] YIELD\n");

            savedState->pc_epc += WORDLEN;
            updateCPUTime();
            copyState(&currentProcess->p_s, savedState);

            insertProcQ(&readyQueue, currentProcess);
            debug_hex("[SYS] YIELD pid back in readyQueue=", (unsigned int)currentProcess->p_pid);
            currentProcess = NULL;

            scheduler();
            break;
        }

        /* Codice non riconosciuto */
        default: {
            debug_hex("[SYS] unknown sysCode -> passUpOrDie(GENERALEXCEPT) sysCode=", (unsigned int)sysCode);
            passUpOrDie(GENERALEXCEPT);
            break;
        }
    }
}


/* -----------------------------------------------------------------------
 * tlbExceptionHandler
 * ----------------------------------------------------------------------- */
static void tlbExceptionHandler(void) {
    debug_print("[TLB] tlbExceptionHandler -> passUpOrDie(PGFAULTEXCEPT)\n");
    passUpOrDie(PGFAULTEXCEPT);
}


/* -----------------------------------------------------------------------
 * programTrapHandler
 * ----------------------------------------------------------------------- */
static void programTrapHandler(void) {
    debug_print("[TRAP] programTrapHandler -> passUpOrDie(GENERALEXCEPT)\n");
    passUpOrDie(GENERALEXCEPT);
}


/* -----------------------------------------------------------------------
 * passUpOrDie
 * ----------------------------------------------------------------------- */
static void passUpOrDie(int exceptionType) {
    debug_hex("[PUD] passUpOrDie exceptionType=", (unsigned int)exceptionType);

    if (currentProcess->p_supportStruct == NULL) {
        debug_hex("[PUD] Die: no supportStruct, terminating pid=", (unsigned int)currentProcess->p_pid);
        updateCPUTime();
        terminateProcess(currentProcess);
        scheduler();
    } else {
        debug_hex("[PUD] Pass Up: supportStruct found, passing to level 3 handler, pid=", (unsigned int)currentProcess->p_pid);
        support_t *sup = currentProcess->p_supportStruct;

        copyState(&sup->sup_exceptState[exceptionType], (state_t *) BIOSDATAPAGE);

        context_t *ctx = &(sup->sup_exceptContext[exceptionType]);
        debug_hex("[PUD] LDCXT stackPtr=", (unsigned int)ctx->stackPtr);
        debug_hex("[PUD] LDCXT pc=", (unsigned int)ctx->pc);
        LDCXT(ctx->stackPtr, ctx->status, ctx->pc);
    }
}