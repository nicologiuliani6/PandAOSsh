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
#include "../phase1/headers/asl.h"
#include "../headers/listx.h"
#define DEBUG_KERNEL 0
#define debug_print(msg) do { if (DEBUG_KERNEL) debug_print(msg); } while(0) 
#define debug_hex(msg, val) do { if (DEBUG_KERNEL) debug_hex(msg, val); } while(0)
/* -----------------------------------------------------------------------
 * Costanti e macro
 * ----------------------------------------------------------------------- */
#ifndef CAUSE_EXCCODE_MASK
#define CAUSE_EXCCODE_MASK 0xFFu  /* Maschera tutti i bit bassi della causa */
#endif

/* -----------------------------------------------------------------------
 * Prototipi interni
 * ----------------------------------------------------------------------- */
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

/* -----------------------------------------------------------------------
 * Utility
 * ----------------------------------------------------------------------- */
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

/* -----------------------------------------------------------------------
 * Blocca il processo corrente sul semaforo
 * ----------------------------------------------------------------------- */
/* -----------------------------------------------------------------------
 * Utility per device semaphore
 * ----------------------------------------------------------------------- */
static int isDeviceSemaphore(int *semAdd) {
    int *base = &devSems[0];
    int *top  = &devSems[TOT_SEMS];
    return (semAdd >= base && semAdd < top);
}

/* -----------------------------------------------------------------------
 * Blocca il processo corrente sul semaforo
 * ----------------------------------------------------------------------- */
static void blockCurrentProcess(int *sem) {
    if (!currentProcess) PANIC();

    cpu_t now;
    STCK(now);
    currentProcess->p_time += now - startTOD;
    startTOD = now;

    currentProcess->p_semAdd = sem;
    copyState(&currentProcess->p_s, (state_t *) BIOSDATAPAGE);

    // Debug avanzato
    debug_print("\n[KERNEL] Blocking PID=");
    debug_hex("", (unsigned int) currentProcess->p_pid);   // solo il PID

    debug_print(" on semaphore addr=");
    debug_hex("", (unsigned int) sem);                     // solo l'indirizzo del semaforo

    debug_print(" value=");
    debug_hex("", (unsigned int) *sem);                    // solo il valore

    if (isDeviceSemaphore(sem)) {
        debug_print(" type=DEVICE ");
        softBlockCount++;
        debug_print(" softBlockCount=");
        debug_hex("", (unsigned int) softBlockCount);      // solo il contatore
    } else {
        debug_print(" type=NORMAL");
    }

    debug_print("\n");

    insertBlocked(sem, currentProcess);
    currentProcess = NULL;
    scheduler(); // non ritorna
}

/* -----------------------------------------------------------------------
 * Gestione lista activeProcs
 * ----------------------------------------------------------------------- */
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

/* -----------------------------------------------------------------------
 * Termina processo e sottoalbero
 * ----------------------------------------------------------------------- */
static void terminateProcess(pcb_t *proc) {
    if (!proc) return;

    pcb_t *child;
    /* 1. Termina ricorsivamente tutti i figli */
    while ((child = removeChild(proc)) != NULL) {
        terminateProcess(child);
    }

    /* 2. Rimuovi dalla lista globale dei processi attivi */
    activeProcs_remove(proc);

    /* 3. Gestione semafori */
    if (proc->p_semAdd != NULL) {
        int *sem = proc->p_semAdd;
        outBlocked(proc);
        proc->p_semAdd = NULL;

        if (isDeviceSemaphore(sem)) {
            softBlockCount--;
        } else {
            /* Se non device semaphore, rilascia il semaforo */
            (*sem)++;
        }
    } else if (proc != currentProcess) {
        /* Se non corrente e non bloccato → rimuovi dalla ready queue */
        outProcQ(&readyQueue, proc);
    }

    /* 4. Stacca dal padre */
    outChild(proc);

    /* 5. Debug */
    debug_print("\n[KERNEL] Terminating PID=");
    debug_hex("", (unsigned int) proc->p_pid);
    debug_print(" processCount=");
    debug_hex("", (unsigned int) processCount);

    /* 7. Se era corrente, azzera currentProcess */
    if (proc == currentProcess) currentProcess = NULL;

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
 * Entry point principale
 * ----------------------------------------------------------------------- */
void exceptionHandler(void) {
    state_t *savedState = (state_t *) BIOSDATAPAGE;
    unsigned int cause   = savedState->cause;
    unsigned int excCode = cause & CAUSE_EXCCODE_MASK;

    cpu_t now;
    STCK(now);
    if (currentProcess) currentProcess->p_time += now - startTOD;
    startTOD = now;

    if (cause & 0x80000000) {
        interruptHandler();
        PANIC();
    }
    else if (excCode == 8 || excCode == 11) {
        savedState->pc_epc += WORDLEN;
        syscallHandler(savedState);
        PANIC();
    }
    else if (excCode == 12 || excCode == 13 || excCode == 15) {
        tlbExceptionHandler();
    }
    else if (excCode == 1 || excCode == 5 || excCode == 7) {
        unsigned int mpp = savedState->status & MSTATUS_MPP_MASK;
        if (mpp == 0) savedState->cause = 5, programTrapHandler();
        else tlbExceptionHandler();
    }
    else {
        programTrapHandler();
    }
    PANIC();
}

/* -----------------------------------------------------------------------
 * Gestione syscall
 * ----------------------------------------------------------------------- */
static void syscallHandler(state_t *savedState) {
    int sysCode = (int) savedState->reg_a0;

    if ((savedState->status & MSTATUS_MPP_MASK) == 0 && sysCode < 0) {
        savedState->cause = PRIVINSTR;
        programTrapHandler();
        return;
    }

    if (sysCode >= 1) {
        savedState->pc_epc += WORDLEN;
        passUpOrDie(GENERALEXCEPT);
        return;
    }

    switch (sysCode) {

        case CREATEPROCESS: {
            state_t   *newState = (state_t *) savedState->reg_a1;
            int        prio     = (int) savedState->reg_a2;
            support_t *support  = (support_t *) savedState->reg_a3;

            pcb_t *child = allocPcb();
            if (!child) savedState->reg_a0 = (unsigned int) -1;
            else {
                copyState(&child->p_s, newState);
                child->p_supportStruct = support;
                child->p_time = 0;
                child->p_semAdd = NULL;
                child->p_prio = prio;

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
        case TERMPROCESS: {
            int targetPid = (int) savedState->reg_a1;
            updateCPUTime();

            pcb_t *target = (targetPid == 0) ? currentProcess : findProcessByPid(targetPid);
            if (!target) {
                /* PID non trovato: avanza PC e torna al chiamante */
                savedState->pc_epc += WORDLEN;
                LDST(savedState); /* Non crasha, LDST sul chiamante */
            } else {
                /* Controlla se stiamo terminando noi stessi */
                int terminatingSelf = (target == currentProcess);

                /* Termina il target e sottoalbero */
                terminateProcess(target);

                /* Se era corrente, azzera currentProcess */
                if (terminatingSelf) currentProcess = NULL;
            }

            /* Sempre passaggio allo scheduler sicuro */
            if (currentProcess == NULL) {
                scheduler();
            } else {
                /* Copia stato aggiornato e riprendi */
                copyState(&currentProcess->p_s, savedState);
                LDST(&currentProcess->p_s);
            }
            break;
        }
        #define DEVICE_BASE_ADDR 0x20009C7C   // primo indirizzo di device semaforo
        #define DEVICE_MAX_ADDR  0x20009D70   // ultimo indirizzo di device semaforo
        case PASSEREN: {
            int *semAddr = (int *) savedState->reg_a1;

            debug_print("\n[KERNEL] PASSEREN called, semAddr=");
            debug_hex("", (unsigned int) semAddr);
            debug_print(" value=");
            debug_hex("", (unsigned int) *semAddr);
            debug_print("\n");

            if (!semAddr) PANIC();

            (*semAddr)--;

            if (*semAddr < 0) {
                if ((unsigned int) semAddr >= DEVICE_BASE_ADDR && (unsigned int) semAddr <= DEVICE_MAX_ADDR) {
                    blockCurrentProcess(semAddr);  // DEVICE semaphore
                } else {
                    blockCurrentProcess(semAddr);  // NORMAL semaphore
                }
            } else {
                savedState->pc_epc += WORDLEN;  // continua il processo
                LDST(savedState);               // riprende esecuzione
            }
            break;
        }

case VERHOGEN: {
    int *semAddr = (int *) savedState->reg_a1;
    (*semAddr)++;

    debug_print("\n[KERNEL] VERHOGEN called, semAddr=");
    debug_hex("", (unsigned int) semAddr);
    debug_print(" value=");
    debug_hex("", (unsigned int) *semAddr);
    debug_print("\n");

    if (*semAddr <= 0) {  // solo se NORMAL con blocchi
        semd_t *s;
        pcb_t *p = NULL;

        list_for_each_entry(s, &semd_h, s_link) {
            if (s->s_key == semAddr && !list_empty(&s->s_procq)) {
                p = removeBlocked(semAddr);
                break;
            }
        }

        if (p) {
            p->p_semAdd = NULL;
            insertProcQ(&readyQueue, p);
            softBlockCount--;

            debug_print("[KERNEL] Unblocking PID=");
            debug_hex("", (unsigned int) p->p_pid);
            debug_print(" softBlockCount=");
            debug_hex("", (unsigned int) softBlockCount);
            debug_print("\n");
        } else {
            debug_print("[KERNEL] No process found blocked on this semaphore!\n");
        }
    }

    savedState->pc_epc += WORDLEN;
    LDST(savedState);
    break;
}

        case DOIO: {
            int *commandAddr  = (int *) savedState->reg_a1;
            int  commandValue = (int) savedState->reg_a2;

            unsigned int offset  = (unsigned int)commandAddr - START_DEVREG;
            int line    = (int)(offset / 0x80) + 3;
            int dev     = (int)((offset % 0x80) / 0x10);
            int subword = (int)((offset % 0x10) / WORDLEN);

            int semIdx = (line == 7) ? ((subword == 3) ? TERM_TX_SEM(dev) : TERM_RX_SEM(dev))
                                    : DEV_SEM_BASE(line, dev);

            /* Scrivo il comando sul device */
            *commandAddr = commandValue;

            savedState->pc_epc += WORDLEN;
            updateCPUTime();
            copyState(&currentProcess->p_s, savedState);

            /* Solo blocco se il device è occupato */
            unsigned int *devBase = (unsigned int *)(START_DEVREG + (line - 3)*0x80 + dev*0x10);
            int ready;
            // DOIO terminale
            if (line == 7) {
                if (subword == 3) { // TX
                    if (!(devBase[3] & 0x1)) { // non pronto
                        devSems[semIdx]--;
                        softBlockCount++;
                        blockCurrentProcess(&devSems[semIdx]);
                    } else {
                        *commandAddr = commandValue; // TX pronto → scrivi
                        devBase[3] = 1; // TX ACK
                    }
                } else { // RX
                    if (!(devBase[1] & 0x1)) { // non pronto
                        devSems[semIdx]--;
                        softBlockCount++;
                        blockCurrentProcess(&devSems[semIdx]);
                    } else {
                        devBase[1] = 1; // RX ACK
                    }
                }
            } else {
                ready = devBase[1] & 0x1; // altri device
            }

            if (!ready) { // il device non è pronto: blocco il processo
                devSems[semIdx]--;
                softBlockCount++;
                blockCurrentProcess(&devSems[semIdx]);
            }



            break;
        }

        case GETTIME: {
            updateCPUTime();
            savedState->reg_a0 = (unsigned int) currentProcess->p_time;
            savedState->pc_epc += WORDLEN;
            LDST(savedState);
            break;
        }

        case CLOCKWAIT: {
            savedState->pc_epc += WORDLEN;
            updateCPUTime();
            copyState(&currentProcess->p_s, savedState);

            devSems[PSEUDOCLK_SEM]--;
            softBlockCount++;
            blockCurrentProcess(&devSems[PSEUDOCLK_SEM]);
            break;
        }

        case GETSUPPORTPTR: {
            savedState->reg_a0 = (unsigned int) currentProcess->p_supportStruct;
            savedState->pc_epc += WORDLEN;
            LDST(savedState);
            break;
        }

        case GETPROCESSID: {
            int wantParent = (int) savedState->reg_a1;
            savedState->reg_a0 = (wantParent == 0)
                ? (unsigned int) currentProcess->p_pid
                : (currentProcess->p_parent ? (unsigned int) currentProcess->p_parent->p_pid : 0);
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

        default: passUpOrDie(GENERALEXCEPT);
    }
}

/* -----------------------------------------------------------------------
 * passUpOrDie
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