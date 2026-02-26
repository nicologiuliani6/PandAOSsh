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

/*
 * copyState - copia uno state_t word per word.
 * Non usiamo l'assegnazione struct perché il compilatore la trasforma
 * in memcpy che non è disponibile in ambiente freestanding.
 */
static void copyState(state_t *dst, state_t *src) {
    unsigned int *d = (unsigned int *) dst;
    unsigned int *s = (unsigned int *) src;
    /* state_t è STATESIZE byte = STATESIZE/4 word */
    for (int i = 0; i < STATESIZE / WORDLEN; i++) {
        d[i] = s[i];
    }
}


/* -----------------------------------------------------------------------
 * NOTA: uTLB_RefillHandler è definita in p2test.c per Phase 2.
 * Il suo indirizzo viene comunque registrato nel Pass Up Vector
 * da initial.c tramite la dichiarazione extern qui sotto.
 * ----------------------------------------------------------------------- */


/* -----------------------------------------------------------------------
 * exceptionHandler
 *
 * Entry point per tutte le eccezioni (interrupt inclusi), esclusi i
 * TLB-Refill. Viene chiamato dal BIOS con stack fresco.
 * Lo stato del processore al momento dell'eccezione è salvato nel
 * BIOS Data Page (BIOSDATAPAGE).
 * ----------------------------------------------------------------------- */
void exceptionHandler(void) {
    /* Recupera lo stato salvato al momento dell'eccezione */
    state_t *savedState = (state_t *) BIOSDATAPAGE;

    unsigned int cause   = savedState->cause;
    /* Estrae il codice eccezione dai bit 6:2 (shift di 2, maschera 0x1F) */
    unsigned int excCode = (cause & GETEXECCODE) >> CAUSESHIFT;

    /* Dispatch in base al tipo di eccezione.
     * In µRISC-V il bit 31 del registro cause = 1 indica un interrupt */
    if (cause & 0x80000000) {
        /* Interrupt (device o timer) */
        interruptHandler();
    } else if (excCode == 8 || excCode == 11) {
        /* SYSCALL (eccezione codice 8 o 11) */
        syscallHandler(savedState);
    } else if (excCode >= 24 && excCode <= 28) {
        /* Eccezioni TLB */
        tlbExceptionHandler();
    } else {
        /* Program Trap (codici 0-7, 9, 10, 12-23) */
        programTrapHandler();
    }
}


/* -----------------------------------------------------------------------
 * updateCPUTime
 *
 * Aggiorna il campo p_time del processo corrente con il tempo CPU
 * accumulato dall'ultimo dispatch fino ad ora.
 * ----------------------------------------------------------------------- */
static void updateCPUTime(void) {
    if (currentProcess != NULL) {
        cpu_t now;
        STCK(now);
        currentProcess->p_time += (now - startTOD);
        /* Resetta startTOD per misure future nel caso il processo
         * riprenda l'esecuzione (es. dopo una SYSCALL non bloccante) */
        startTOD = now;
    }
}


/* -----------------------------------------------------------------------
 * terminateProcess
 *
 * Termina ricorsivamente proc e tutti i suoi discendenti.
 * Gestisce i tre stati possibili di ogni PCB:
 *   - Running    : è currentProcess
 *   - Ready      : è nella Ready Queue
 *   - Blocked    : è bloccato su un semaforo (device o generico)
 * ----------------------------------------------------------------------- */
static void terminateProcess(pcb_t *proc) {
    if (proc == NULL) return;

    /* Termina prima tutti i figli (ricorsione sul sottoalbero) */
    while (!emptyChild(proc)) {
        terminateProcess(removeChild(proc));
    }

    /* Aggiusta processCount e softBlockCount */
    processCount--;

    /* Rimuove il PCB dalla struttura in cui si trova */
    if (proc == currentProcess) {
        /* È il processo in esecuzione: verrà eliminato, il chiamante
         * chiamerà lo scheduler */
        currentProcess = NULL;
    } else if (proc->p_semAdd != NULL) {
        /* È bloccato su un semaforo */
        outBlocked(proc);

        /* Se bloccato su un semaforo device/pseudo-clock, decrementa
         * softBlockCount. Un semaforo è "device" se il suo indirizzo
         * ricade nell'array devSems. */
        for (int i = 0; i < TOT_SEMS; i++) {
            if (&devSems[i] == proc->p_semAdd) {
                softBlockCount--;
                break;
            }
        }
    } else {
        /* È nella Ready Queue */
        outProcQ(&readyQueue, proc);
    }

    /* "Orfana" il PCB dal genitore */
    outChild(proc);

    /* Restituisce il PCB alla free list */
    freePcb(proc);
}


/* -----------------------------------------------------------------------
 * syscallHandler
 *
 * Gestisce le SYSCALL con codice negativo (NSYS1..NSYS10) in kernel-mode.
 * Se il processo è in user-mode, simula una Program Trap (PRIVINSTR).
 * Se il codice è positivo o non riconosciuto → Pass Up or Die.
 * ----------------------------------------------------------------------- */
static void syscallHandler(state_t *savedState) {
    int sysCode = (int) savedState->reg_a0;

    /* ------------------------------------------------------------------
     * Controllo modalità: i servizi negativi sono privilegiati.
     * Se il processo è in user-mode → simula PRIVINSTR e chiama
     * il program trap handler.
     * ------------------------------------------------------------------ */
    if ((savedState->status & MSTATUS_MPP_MASK) == 0) {
        /* Processo in user-mode */
        if (sysCode < 0) {
            /* Simula Program Trap per istruzione privilegiata */
            savedState->cause = PRIVINSTR;
            programTrapHandler();
            return;
        }
    }

    /* ------------------------------------------------------------------
     * Se il codice è positivo (o zero): Pass Up or Die (GENERALEXCEPT)
     * ------------------------------------------------------------------ */
    if (sysCode >= 1) {
        passUpOrDie(GENERALEXCEPT);
        return;
    }

    /* ------------------------------------------------------------------
     * Per le SYSCALL non bloccanti e non terminanti:
     * incrementa PC di 4 per evitare il loop infinito di SYSCALL.
     * Per quelle bloccanti il PC viene incrementato prima del blocco.
     * ------------------------------------------------------------------ */

    switch (sysCode) {

        /* --------------------------------------------------------------
         * NSYS1 - CREATEPROCESS
         * Crea un nuovo processo figlio del processo corrente.
         * a1 = state_t*, a2 = priorità, a3 = support_t*
         * Ritorna il PID del nuovo processo in a0, o -1 se errore.
         * -------------------------------------------------------------- */
        case CREATEPROCESS: {
            state_t   *newState   = (state_t *)   savedState->reg_a1;
            int        prio       = (int)          savedState->reg_a2;
            support_t *supportPtr = (support_t *)  savedState->reg_a3;

            pcb_t *child = allocPcb();
            if (child == NULL) {
                /* Nessun PCB disponibile */
                savedState->reg_a0 = (unsigned int) -1;
            } else {
                /* Inizializza il nuovo PCB */
                copyState(&child->p_s, newState);
                child->p_supportStruct = supportPtr;
                child->p_time         = 0;
                child->p_semAdd       = NULL;
                child->p_prio         = prio;

                /* Inserisce nella Ready Queue e nell'albero processi */
                insertProcQ(&readyQueue, child);
                insertChild(currentProcess, child);

                processCount++;

                /* Ritorna il PID del figlio */
                savedState->reg_a0 = (unsigned int) child->p_pid;
            }

            /* Ritorna il controllo al processo corrente */
            savedState->pc_epc += WORDLEN;
            LDST(savedState);
            break;
        }

        /* --------------------------------------------------------------
         * NSYS2 - TERMINATEPROCESS
         * Termina il processo corrente (pid==0) o il processo con PID
         * indicato, insieme a tutti i suoi discendenti.
         * -------------------------------------------------------------- */
        case TERMPROCESS: {
            int targetPid = (int) savedState->reg_a1;

            if (targetPid == 0) {
                /* Termina il processo corrente */
                updateCPUTime();
                terminateProcess(currentProcess);
            } else {
                /*
                 * Cerca il processo con PID targetPid.
                 * Strategia: scansiona la Ready Queue e l'ASL.
                 * Per semplicità, usa una funzione ausiliaria.
                 */

                /* Cerca nella Ready Queue */
                pcb_t *target = NULL;
                struct list_head *pos;
                list_for_each(pos, &readyQueue) {
                    pcb_t *p = container_of(pos, pcb_t, p_list);
                    if (p->p_pid == (unsigned int) targetPid) {
                        target = p;
                        break;
                    }
                }

                /* Se non trovato in Ready Queue, cerca tra i bloccati
                 * controllando tutti i semafori device */
                if (target == NULL) {
                    for (int i = 0; i < TOT_SEMS && target == NULL; i++) {
                        pcb_t *p = headBlocked(&devSems[i]);
                        while (p != NULL) {
                            if (p->p_pid == (unsigned int) targetPid) {
                                target = p;
                                break;
                            }
                            /* Avanza nella lista dei bloccati sullo stesso sem */
                            struct list_head *next = p->p_list.next;
                            if (next == &(((semd_t*)NULL)->s_procq)) break;
                            p = container_of(next, pcb_t, p_list);
                        }
                    }
                }

                /* Potrebbe essere il processo corrente stesso */
                if (target == NULL && currentProcess != NULL &&
                    currentProcess->p_pid == (unsigned int) targetPid) {
                    target = currentProcess;
                }

                if (target != NULL) {
                    /* Se stiamo terminando il processo corrente */
                    if (target == currentProcess) {
                        updateCPUTime();
                    }
                    terminateProcess(target);
                }
                /* Se non trovato: ignora (già terminato) */
            }

            /* Dopo terminate, chiama lo scheduler */
            scheduler();
            break;
        }

        /* --------------------------------------------------------------
         * NSYS3 - PASSEREN (operazione P su semaforo)
         * a1 = indirizzo del semaforo
         * Bloccante se semaforo <= 0.
         * -------------------------------------------------------------- */
        case PASSEREN: {
            int *semAddr = (int *) savedState->reg_a1;

            /* Incrementa PC prima di bloccarsi */
            savedState->pc_epc += WORDLEN;

            (*semAddr)--;
            if (*semAddr < 0) {
                /* Blocca il processo corrente */
                updateCPUTime();
                copyState(&currentProcess->p_s, savedState);
                insertBlocked(semAddr, currentProcess);
                currentProcess = NULL;
                scheduler();
            } else {
                /* Non bloccante: ritorna al processo */
                LDST(savedState);
            }
            break;
        }

        /* --------------------------------------------------------------
         * NSYS4 - VERHOGEN (operazione V su semaforo)
         * a1 = indirizzo del semaforo
         * Non bloccante.
         * -------------------------------------------------------------- */
        case VERHOGEN: {
            int *semAddr = (int *) savedState->reg_a1;

            (*semAddr)++;
            if (*semAddr <= 0) {
                /* Sblocca il primo processo in attesa */
                pcb_t *unblocked = removeBlocked(semAddr);
                if (unblocked != NULL) {
                    unblocked->p_semAdd = NULL;
                    insertProcQ(&readyQueue, unblocked);
                }
            }

            /* Ritorna al processo corrente */
            savedState->pc_epc += WORDLEN;
            LDST(savedState);
            break;
        }

        /* --------------------------------------------------------------
         * NSYS5 - DOIO
         * Avvia un'operazione I/O sincrona e blocca il processo.
         * a1 = indirizzo del campo COMMAND del device register
         * a2 = valore da scrivere nel campo COMMAND
         *
         * Calcola il semaforo corrispondente dall'indirizzo del device
         * register, esegue P su di esso (sempre bloccante), poi scrive
         * il comando nel registro device.
         * -------------------------------------------------------------- */
        case DOIO: {
            int *commandAddr  = (int *) savedState->reg_a1;
            int  commandValue = (int)   savedState->reg_a2;

            /*
             * Determina il semaforo corrispondente al device.
             * L'indirizzo base dei device register è 0x10000054.
             * devAddrBase = 0x10000054 + (IntLineNo-3)*0x80 + DevNo*0x10
             *
             * Per terminali:
             *   TRANSM_COMMAND è a offset +0xC dal base del device
             *   RECV_COMMAND   è a offset +0x4 dal base del device
             *
             * Inversione: dall'indirizzo del command ricaviamo linea e device.
             */
            int devOffset = (int)commandAddr - START_DEVREG;
            int lineOffset, devNo, semIdx;

            /* Ogni linea occupa 8 device * 0x10 = 0x80 byte */
            lineOffset = devOffset / 0x80;         /* quale gruppo di linea */
            int withinLine = devOffset % 0x80;
            devNo = withinLine / 0x10;             /* quale device nella linea */
            int withinDev = withinLine % 0x10;     /* offset dentro il device */

            int intLineNo = lineOffset + 3;

            if (intLineNo == IL_TERMINAL) {
                /* Terminale: distingue TX (TRANSM_COMMAND = +0xC) da RX (+0x4) */
                if (withinDev == 0xC) {
                    /* Trasmissione */
                    semIdx = TERM_TX_SEM(devNo);
                } else {
                    /* Ricezione */
                    semIdx = TERM_RX_SEM(devNo);
                }
            } else {
                semIdx = DEV_SEM_BASE(intLineNo, devNo);
            }

            /* Incrementa PC prima di bloccarsi */
            savedState->pc_epc += WORDLEN;

            /* Aggiorna CPU time e salva stato */
            updateCPUTime();
            copyState(&currentProcess->p_s, savedState);

            /* P sul semaforo del device (sempre bloccante per DOIO) */
            devSems[semIdx]--;
            insertBlocked(&devSems[semIdx], currentProcess);
            softBlockCount++;
            currentProcess = NULL;

            /* Scrive il comando nel device register (avvia l'I/O) */
            *commandAddr = commandValue;

            /* Chiama lo scheduler */
            scheduler();
            break;
        }

        /* --------------------------------------------------------------
         * NSYS6 - GETCPUTIME
         * Ritorna il tempo CPU accumulato dal processo corrente in a0.
         * Include il tempo del quanto corrente (dall'ultimo dispatch).
         * -------------------------------------------------------------- */
        case GETTIME: {
            cpu_t now;
            STCK(now);
            /* p_time + tempo del quanto corrente */
            savedState->reg_a0 = currentProcess->p_time + (now - startTOD);

            savedState->pc_epc += WORDLEN;
            LDST(savedState);
            break;
        }

        /* --------------------------------------------------------------
         * NSYS7 - WAITCLOCK
         * Esegue P sul semaforo pseudo-clock (sempre bloccante).
         * Il semaforo viene V'ato ogni 100ms dall'Interval Timer.
         * -------------------------------------------------------------- */
        case CLOCKWAIT: {
            /* Incrementa PC prima di bloccarsi */
            savedState->pc_epc += WORDLEN;

            /* Aggiorna CPU time e salva stato */
            updateCPUTime();
            copyState(&currentProcess->p_s, savedState);

            /* P sul semaforo pseudo-clock */
            devSems[PSEUDOCLK_SEM]--;
            insertBlocked(&devSems[PSEUDOCLK_SEM], currentProcess);
            softBlockCount++;
            currentProcess = NULL;

            scheduler();
            break;
        }

        /* --------------------------------------------------------------
         * NSYS8 - GETSUPPORTPTR
         * Ritorna il puntatore alla Support Structure del processo
         * corrente (può essere NULL).
         * -------------------------------------------------------------- */
        case GETSUPPORTPTR: {
            savedState->reg_a0 = (unsigned int) currentProcess->p_supportStruct;

            savedState->pc_epc += WORDLEN;
            LDST(savedState);
            break;
        }

        /* --------------------------------------------------------------
         * NSYS9 - GETPROCESSID
         * a1 == 0: ritorna PID del processo corrente
         * a1 != 0: ritorna PID del processo padre (0 se radice)
         * -------------------------------------------------------------- */
        case GETPROCESSID: {
            int parent = (int) savedState->reg_a1;

            if (parent == 0) {
                savedState->reg_a0 = (unsigned int) currentProcess->p_pid;
            } else {
                if (currentProcess->p_parent != NULL) {
                    savedState->reg_a0 = (unsigned int) currentProcess->p_parent->p_pid;
                } else {
                    savedState->reg_a0 = 0; /* processo radice: nessun padre */
                }
            }

            savedState->pc_epc += WORDLEN;
            LDST(savedState);
            break;
        }

        /* --------------------------------------------------------------
         * NSYS10 - YIELD
         * Il processo cede il processore.
         * Se ci sono altri processi pronti, non viene rieseguito
         * immediatamente anche se ha la priorità più alta.
         * -------------------------------------------------------------- */
        case YIELD: {
            /* Incrementa PC prima di bloccarsi */
            savedState->pc_epc += WORDLEN;

            /* Aggiorna CPU time e salva stato nel PCB */
            updateCPUTime();
            copyState(&currentProcess->p_s, savedState);

            /* Rimette il processo in coda alla Ready Queue */
            insertProcQ(&readyQueue, currentProcess);
            currentProcess = NULL;

            scheduler();
            break;
        }

        /* --------------------------------------------------------------
         * Codice non riconosciuto → Pass Up or Die
         * -------------------------------------------------------------- */
        default: {
            passUpOrDie(GENERALEXCEPT);
            break;
        }
    }
}


/* -----------------------------------------------------------------------
 * tlbExceptionHandler
 *
 * Gestisce le eccezioni TLB (codici 24-28): Pass Up or Die con
 * indice PGFAULTEXCEPT.
 * ----------------------------------------------------------------------- */
static void tlbExceptionHandler(void) {
    passUpOrDie(PGFAULTEXCEPT);
}


/* -----------------------------------------------------------------------
 * programTrapHandler
 *
 * Gestisce le Program Trap (codici 0-7, 9, 10, 12-23): Pass Up or Die
 * con indice GENERALEXCEPT.
 * ----------------------------------------------------------------------- */
static void programTrapHandler(void) {
    passUpOrDie(GENERALEXCEPT);
}


/* -----------------------------------------------------------------------
 * passUpOrDie
 *
 * Implementa il meccanismo "Pass Up or Die":
 *  - Se p_supportStruct == NULL: termina il processo (Die)
 *  - Altrimenti: copia lo stato salvato nel sup_exceptState e chiama
 *    il handler del Support Level tramite LDCXT (Pass Up)
 *
 * exceptionType: PGFAULTEXCEPT (0) o GENERALEXCEPT (1)
 * ----------------------------------------------------------------------- */
static void passUpOrDie(int exceptionType) {
    if (currentProcess->p_supportStruct == NULL) {
        /* Die: termina il processo e tutto il suo sottoalbero */
        updateCPUTime();
        terminateProcess(currentProcess);
        scheduler();
    } else {
        /* Pass Up: trasferisce la gestione al Support Level */
        support_t *sup = currentProcess->p_supportStruct;

        /* Copia lo stato salvato nell'eccezione nel campo appropriato
         * della Support Structure */
        copyState(&sup->sup_exceptState[exceptionType], (state_t *) BIOSDATAPAGE);

        /* Carica il contesto del Support Level handler */
        context_t *ctx = &(sup->sup_exceptContext[exceptionType]);
        LDCXT(ctx->stackPtr, ctx->status, ctx->pc);
    }
}