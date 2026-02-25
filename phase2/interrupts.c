/*
 * interrupts.c
 *
 * Gestisce tutti gli interrupt di device e timer:
 *
 *  - PLT (Processor Local Timer, linea 1):
 *    Il quanto di tempo del processo corrente è scaduto.
 *    → Rimette il processo nella Ready Queue e chiama lo Scheduler.
 *
 *  - Interval Timer (linea 2, Pseudo-clock):
 *    Ogni 100ms: ricarica il timer, sblocca tutti i processi in
 *    attesa del Pseudo-clock tick.
 *
 *  - Device interrupt (linee 3-7):
 *    Acknowledge del device, V sul semaforo del device,
 *    status word al processo sbloccato.
 *
 * Priorità: PLT > Interval Timer > linea 3 dev 0 > ... > linea 7 dev 7 (RX)
 */

#include "../headers/const.h"
#include "../headers/types.h"
#include <uriscv/liburiscv.h>

#include "../phase1/headers/pcb.h"
#include "../phase1/headers/asl.h"
#include "./headers/globals.h"

/* Dichiarazione scheduler */
extern void scheduler(void);


/* -----------------------------------------------------------------------
 * Costanti locali per i device register
 * ----------------------------------------------------------------------- */

/* Indirizzo base del bitmap degli interrupt pendenti (linee 3-7) */
#define INT_BITMAP_BASE  0x10000040

/* Macro per ricavare il bitmap della linea `line` (3..7) */
#define INT_BITMAP(line) (*((unsigned int *)(INT_BITMAP_BASE + ((line) - 3) * 0x4)))

/* Indirizzo base del primo device register */
#define DEV_REG_ADDR(line, dev) \
    ((devreg *)(DEV_REG_START + ((line) - 3) * 0x80 + (dev) * 0x10))


/* -----------------------------------------------------------------------
 * getHighestPriorityDevice
 *
 * Data la bitmap degli interrupt pendenti di una linea, restituisce
 * il numero del device con priorità più alta (il bit più basso impostato).
 * ----------------------------------------------------------------------- */
static int getHighestPriorityDevice(unsigned int bitmap) {
    for (int i = 0; i < 8; i++) {
        if (bitmap & (1 << i)) return i;
    }
    return -1; /* nessun device pendente */
}


/* -----------------------------------------------------------------------
 * interruptHandler
 *
 * Entry point per la gestione degli interrupt. Determina il tipo di
 * interrupt con la priorità più alta e lo gestisce.
 * ----------------------------------------------------------------------- */
void interruptHandler(void) {
    /* Stato salvato al momento dell'eccezione */
    state_t *savedState = (state_t *) BIOSDATAPAGE;

    /* Causa dell'eccezione */
    unsigned int cause = getCAUSE();

    /* ------------------------------------------------------------------
     * Priorità 1: PLT (IL_CPUTIMER, linea interrupt 1)
     * Il processo corrente ha esaurito il suo time slice.
     * ------------------------------------------------------------------ */
    if (cause & (1 << IL_CPUTIMER)) {
        /* Acknowledge: ricarica il PLT con un valore arbitrario grande */
        setTIMER(TIMESLICE * (*((cpu_t *)TIMESCALEADDR)));

        /* Aggiorna il tempo CPU del processo corrente */
        if (currentProcess != NULL) {
            cpu_t now;
            STCK(now);
            currentProcess->p_time += (now - startTOD);

            /* Salva lo stato del processo nel PCB */
            currentProcess->p_s = *savedState;

            /* Rimette il processo nella Ready Queue */
            insertProcQ(&readyQueue, currentProcess);
            currentProcess = NULL;
        }

        /* Chiama lo scheduler */
        scheduler();
        return; /* non si arriva qui */
    }

    /* ------------------------------------------------------------------
     * Priorità 2: Interval Timer (IL_TIMER, linea 2) → Pseudo-clock Tick
     * ------------------------------------------------------------------ */
    if (cause & (1 << IL_TIMER)) {
        /* Acknowledge: ricarica l'Interval Timer con 100ms */
        LDIT(PSECOND);

        /* Sblocca TUTTI i processi in attesa del Pseudo-clock */
        pcb_t *unblocked;
        while ((unblocked = removeBlocked(&devSems[PSEUDOCLK_SEM])) != NULL) {
            unblocked->p_semAdd = NULL;
            /* Imposta a0 = 0 (nessun status word per il pseudo-clock) */
            unblocked->p_s.reg_a0 = 0;
            insertProcQ(&readyQueue, unblocked);
            softBlockCount--;
        }
        /* Reimposta il semaforo pseudo-clock a 0 */
        devSems[PSEUDOCLK_SEM] = 0;

        /* Ritorna al processo corrente (o allo scheduler se WAIT) */
        if (currentProcess != NULL) {
            LDST(savedState);
        } else {
            scheduler();
        }
        return;
    }

    /* ------------------------------------------------------------------
     * Priorità 3-7: Device interrupt (linee 3-7)
     * Processa il singolo interrupt con priorità più alta.
     * ------------------------------------------------------------------ */
    int intLineNo = -1;
    int devNo     = -1;
    int semIdx    = -1;
    int isTx      = 0; /* per terminali: 1=trasmissione, 0=ricezione */

    /* Scansiona le linee in ordine di priorità crescente (3=alta, 7=bassa) */
    for (int line = IL_DISK; line <= IL_TERMINAL; line++) {
        unsigned int bitmap = INT_BITMAP(line);
        if (bitmap != 0) {
            intLineNo = line;
            devNo = getHighestPriorityDevice(bitmap);
            break;
        }
    }

    if (intLineNo == -1 || devNo == -1) {
        /* Nessun interrupt device pendente: ritorna */
        if (currentProcess != NULL) {
            LDST(savedState);
        } else {
            scheduler();
        }
        return;
    }

    /* ------------------------------------------------------------------
     * Gestione interrupt device
     * ------------------------------------------------------------------ */

    if (intLineNo == IL_TERMINAL) {
        /*
         * Terminale: due sub-device indipendenti.
         * Il trasmettitore (TX) ha priorità sul ricevitore (RX).
         * Struttura terminal device register:
         *   +0x0 RECV_STATUS
         *   +0x4 RECV_COMMAND
         *   +0x8 TRANSM_STATUS
         *   +0xC TRANSM_COMMAND
         */
        termreg_t *termReg = (termreg_t *) DEV_REG_ADDR(intLineNo, devNo);

        unsigned int txStatus = termReg->transm_status & 0xFF;
        unsigned int rxStatus = termReg->recv_status   & 0xFF;

        /* TX ha priorità: controlla se TX ha un interrupt pendente */
        if (txStatus != READY && txStatus != BUSY) {
            /* Interrupt di trasmissione */
            unsigned int savedStatus = termReg->transm_status;

            /* Acknowledge: scrive ACK nel comando TX */
            termReg->transm_command = ACK;

            semIdx = TERM_TX_SEM(devNo);
            isTx   = 1;

            devSems[semIdx]++;
            if (devSems[semIdx] <= 0) {
                pcb_t *unblocked = removeBlocked(&devSems[semIdx]);
                if (unblocked != NULL) {
                    unblocked->p_s.reg_a0 = savedStatus;
                    unblocked->p_semAdd   = NULL;
                    insertProcQ(&readyQueue, unblocked);
                    softBlockCount--;
                }
            }
        } else if (rxStatus != READY && rxStatus != BUSY) {
            /* Interrupt di ricezione */
            unsigned int savedStatus = termReg->recv_status;

            /* Acknowledge */
            termReg->recv_command = ACK;

            semIdx = TERM_RX_SEM(devNo);

            devSems[semIdx]++;
            if (devSems[semIdx] <= 0) {
                pcb_t *unblocked = removeBlocked(&devSems[semIdx]);
                if (unblocked != NULL) {
                    unblocked->p_s.reg_a0 = savedStatus;
                    unblocked->p_semAdd   = NULL;
                    insertProcQ(&readyQueue, unblocked);
                    softBlockCount--;
                }
            }
        }

    } else {
        /*
         * Device non-terminale (disk, flash, ethernet, printer).
         * Struttura standard:
         *   +0x0 STATUS
         *   +0x4 COMMAND
         *   +0x8 DATA0
         *   +0xC DATA1
         */
        devreg_t *devReg = (devreg_t *) DEV_REG_ADDR(intLineNo, devNo);

        /* Salva il codice di status */
        unsigned int savedStatus = devReg->d_status;

        /* Acknowledge */
        devReg->d_command = ACK;

        semIdx = DEV_SEM_BASE(intLineNo, devNo);

        /* V sul semaforo del device */
        devSems[semIdx]++;
        if (devSems[semIdx] <= 0) {
            pcb_t *unblocked = removeBlocked(&devSems[semIdx]);
            if (unblocked != NULL) {
                /* Passa lo status word al processo sbloccato in a0 */
                unblocked->p_s.reg_a0 = savedStatus;
                unblocked->p_semAdd   = NULL;
                insertProcQ(&readyQueue, unblocked);
                softBlockCount--;
            }
        }
    }

    /* Ritorna al processo corrente, oppure chiama lo scheduler */
    if (currentProcess != NULL) {
        LDST(savedState);
    } else {
        scheduler();
    }
}
