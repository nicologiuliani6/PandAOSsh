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

/* Copia uno state_t word per word (memcpy non disponibile in freestanding) */
static void copyState(state_t *dst, state_t *src) {
    unsigned int *d = (unsigned int *) dst;
    unsigned int *s = (unsigned int *) src;
    for (int i = 0; i < STATESIZE / WORDLEN; i++)
        d[i] = s[i];
}


/* -----------------------------------------------------------------------
 * Costanti locali per i device register
 * ----------------------------------------------------------------------- */

/* Indirizzo base del bitmap degli interrupt pendenti (linee 3-7) */
#define INT_BITMAP_BASE  0x10000040

/* Macro per ricavare il bitmap della linea `line` (3..7) */
#define INT_BITMAP(line) (*((unsigned int *)(INT_BITMAP_BASE + ((line) - 3) * 0x4)))

/*
 * Indirizzo base del device register di una linea/device.
 * Ogni linea ha 8 device, ogni device register è 0x10 byte.
 * Offset campi (device NON terminale):
 *   +0x0 STATUS, +0x4 COMMAND, +0x8 DATA0, +0xC DATA1
 * Offset campi terminale:
 *   +0x0 RECV_STATUS, +0x4 RECV_COMMAND
 *   +0x8 TRANSM_STATUS, +0xC TRANSM_COMMAND
 * Usiamo unsigned int* per evitare dipendenze dalle struct di uriscv.
 */
#define DEV_REG_BASE(line, dev) \
    ((unsigned int *)(START_DEVREG + ((line) - 3) * 0x80 + (dev) * 0x10))

/* Offset campi device non-terminale */
#define DEV_STATUS(base)   ((base)[0])
#define DEV_COMMAND(base)  ((base)[1])

/* Offset campi terminale */
#define TERM_RECV_STATUS(base)    ((base)[0])
#define TERM_RECV_COMMAND(base)   ((base)[1])
#define TERM_TRANSM_STATUS(base)  ((base)[2])
#define TERM_TRANSM_COMMAND(base) ((base)[3])


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
     * Priorità 1: PLT (IL_CPUTIMER, linea 7)
     * Il processo corrente ha esaurito il suo time slice.
     * ------------------------------------------------------------------ */
    if (CAUSE_IP_GET(cause, IL_CPUTIMER)) {
        /* Acknowledge: ricarica il PLT con un valore arbitrario grande */
        setTIMER(TIMESLICE * (*((cpu_t *)TIMESCALEADDR)));

        /* Aggiorna il tempo CPU del processo corrente */
        if (currentProcess != NULL) {
            cpu_t now;
            STCK(now);
            currentProcess->p_time += (now - startTOD);

            /* Salva lo stato del processo nel PCB */
            copyState(&currentProcess->p_s, savedState);

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
    if (CAUSE_IP_GET(cause, IL_TIMER)) {
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
         * TX ha priorità su RX.
         * Layout registro (offset in word):
         *   [0] RECV_STATUS, [1] RECV_COMMAND
         *   [2] TRANSM_STATUS, [3] TRANSM_COMMAND
         */
        unsigned int *termBase = DEV_REG_BASE(intLineNo, devNo);

        unsigned int txStatus = TERM_TRANSM_STATUS(termBase) & 0xFF;
        unsigned int rxStatus = TERM_RECV_STATUS(termBase)   & 0xFF;

        /* TX ha priorità: controlla se TX ha un interrupt pendente */
        if (txStatus != READY && txStatus != BUSY) {
            /* Interrupt di trasmissione */
            unsigned int savedStatus = TERM_TRANSM_STATUS(termBase);

            /* Acknowledge TX */
            TERM_TRANSM_COMMAND(termBase) = ACK;

            semIdx = TERM_TX_SEM(devNo);

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
            unsigned int savedStatus = TERM_RECV_STATUS(termBase);

            /* Acknowledge RX */
            TERM_RECV_COMMAND(termBase) = ACK;

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
         * Layout registro (offset in word):
         *   [0] STATUS, [1] COMMAND, [2] DATA0, [3] DATA1
         */
        unsigned int *devBase = DEV_REG_BASE(intLineNo, devNo);

        /* Salva il codice di status */
        unsigned int savedStatus = DEV_STATUS(devBase);

        /* Acknowledge */
        DEV_COMMAND(devBase) = ACK;

        semIdx = DEV_SEM_BASE(intLineNo, devNo);

        /* V sul semaforo del device */
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

    /* Ritorna al processo corrente, oppure chiama lo scheduler */
    if (currentProcess != NULL) {
        LDST(savedState);
    } else {
        scheduler();
    }
}