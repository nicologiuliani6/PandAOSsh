/*
 * interrupts.c - Phase 2 (Nucleus Interrupt Handler)
 *
 * Key fixes:
 *  - Decode interrupt source from CAUSE excCode (not from MIP).
 *  - Handle exactly ONE interrupt per entry (as per spec priority loop behavior).
 *  - Avoid debug prints inside interrupt handler (they generate terminal TX interrupts).
 */

#include "../headers/const.h"
#include "../headers/types.h"
#include <uriscv/liburiscv.h>

#include "../phase1/headers/pcb.h"
#include "../phase1/headers/asl.h"
#include "./headers/globals.h"
#include "debug.h"

extern void scheduler(void);

/* If your headers don't define it, we provide a safe mask for the low excCode bits. */
#ifndef CAUSE_EXCCODE_MASK
#define CAUSE_EXCCODE_MASK 0xFFu
#endif

/* Set to 1 only for short debugging sessions (prints can cause terminal IRQ storms). */
#define DEBUG_INT 0

#if DEBUG_INT
#define IDBG(msg)            debug_print(msg)
#define IDBG_HEX(msg, val)   debug_hex(msg, val)
#else
#define IDBG(msg)            do {} while (0)
#define IDBG_HEX(msg, val)   do {} while (0)
#endif

static void copyState(state_t *dst, state_t *src) {
    unsigned int *d = (unsigned int *) dst;
    unsigned int *s = (unsigned int *) src;
    for (int i = 0; i < STATE_T_SIZE_IN_BYTES / WORDLEN; i++) {
        d[i] = s[i];
    }
}

/* Interrupting Devices Bit Map - spec:
 * Base 0x10000040, one word per interrupt line (lines 3-7).
 * Bit i set = device i on that line has a pending interrupt.
 */
#define INT_BITMAP_BASE  0x10000040
#define INT_BITMAP(line) (*((unsigned int *)(INT_BITMAP_BASE + ((line) - 3) * 0x4)))

/* Device register base address:
 * devAddrBase = START_DEVREG + ((IntLineNo - 3) * 0x80) + (DevNo * 0x10)
 */
#define DEV_REG_BASE(line, dev) \
    ((unsigned int *)(START_DEVREG + ((line) - 3) * 0x80 + (dev) * 0x10))

#define DEV_STATUS(base)          ((base)[0])
#define DEV_COMMAND(base)         ((base)[1])

/* Terminal has 2 subdevices: recv and transm */
#define TERM_RECV_STATUS(base)    ((base)[0])
#define TERM_RECV_COMMAND(base)   ((base)[1])
#define TERM_TRANSM_STATUS(base)  ((base)[2])
#define TERM_TRANSM_COMMAND(base) ((base)[3])

static int getHighestPriorityDevice(unsigned int bitmap) {
    for (int i = 0; i < 8; i++) {
        if (bitmap & (1u << i)) return i;
    }
    return -1;
}

void interruptHandler(void) {

    state_t *savedState = (state_t *) BIOSDATAPAGE;

    unsigned int cause   = savedState->cause;
    unsigned int excCode = cause & CAUSE_EXCCODE_MASK;

    /* Optional debug (careful: printing here can create terminal interrupts!) */
    IDBG("\n[INT] ===== Interrupt received =====\n");
    IDBG_HEX("[INT] CAUSE=", cause);
    IDBG_HEX("[INT] excCode=", excCode);

    /* ================================================================ */
    /* PLT interrupt (excCode == 7)                                     */
    /* ================================================================ */
    if (excCode == 7u) {

        /* Ack/disarm PLT */
        setTIMER((cpu_t) NEVER);

        if (currentProcess != NULL) {

            /* Update CPU time */
            cpu_t now;
            STCK(now);
            currentProcess->p_time += (now - startTOD);

            /* Save process state at exception time */
            copyState(&currentProcess->p_s, savedState);

            /* Ensure interrupts enabled when process resumes */
            currentProcess->p_s.status |= MSTATUS_MIE_MASK;

            /* Round-robin: put back in ready queue */
            insertProcQ(&readyQueue, currentProcess);
            currentProcess = NULL;
        }

        scheduler();
        return;
    }

    /* ================================================================ */
    /* Interval timer (pseudo-clock) (excCode == 3)                     */
    /* ================================================================ */
    if (excCode == 3u) {

        /* Ack interval timer */
        LDIT(PSECOND);

        pcb_t *p;
        while ((p = removeBlocked(&devSems[PSEUDOCLK_SEM])) != NULL) {
            p->p_semAdd   = NULL;
            p->p_s.reg_a0 = 0;
            insertProcQ(&readyQueue, p);
            softBlockCount--;
        }

        devSems[PSEUDOCLK_SEM] = 0;

        if (currentProcess != NULL) {
            LDST(savedState);
        } else {
            scheduler();
        }
        return;
    }

    /* ================================================================ */
    /* Device interrupts: excCode 17..21 => lines 3..7                  */
    /* ================================================================ */
    if (excCode >= 17u && excCode <= 21u) {

        int intLineNo = (int)(excCode - 14u); /* 17->3 ... 21->7 */
        unsigned int bitmap = INT_BITMAP(intLineNo);
        int devNo = getHighestPriorityDevice(bitmap);

        if (devNo < 0) {
            /* Spurious: nothing in bitmap */
            if (currentProcess != NULL) LDST(savedState);
            else scheduler();
            return;
        }

        /* Terminal line is 7 */
        if (intLineNo == 7) {

            unsigned int *termBase = DEV_REG_BASE(intLineNo, devNo);
            unsigned int txStatus = TERM_TRANSM_STATUS(termBase) & 0xFFu;
            unsigned int rxStatus = TERM_RECV_STATUS(termBase) & 0xFFu;

            /* TX has priority over RX */
            if (txStatus != READY && txStatus != BUSY) {

                unsigned int savedStatus = TERM_TRANSM_STATUS(termBase);
                TERM_TRANSM_COMMAND(termBase) = ACK;

                int semIdx = TERM_TX_SEM(devNo);

                if (devSems[semIdx] < 0) {
                    devSems[semIdx]++;
                    pcb_t *unblocked = removeBlocked(&devSems[semIdx]);
                    if (unblocked != NULL) {
                        unblocked->p_s.reg_a0 = savedStatus;
                        unblocked->p_semAdd   = NULL;
                        insertProcQ(&readyQueue, unblocked);
                        softBlockCount--;
                    }
                }
            }

            if (rxStatus != READY && rxStatus != BUSY) {

                unsigned int savedStatus = TERM_RECV_STATUS(termBase);
                TERM_RECV_COMMAND(termBase) = ACK;

                int semIdx = TERM_RX_SEM(devNo);

                if (devSems[semIdx] < 0) {
                    devSems[semIdx]++;
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

            /* Other device lines */
            unsigned int *devBase = DEV_REG_BASE(intLineNo, devNo);
            unsigned int savedStatus = DEV_STATUS(devBase);

            /* Ack */
            DEV_COMMAND(devBase) = ACK;

            int semIdx = DEV_SEM_BASE(intLineNo, devNo);

            if (devSems[semIdx] < 0) {
                devSems[semIdx]++;
                pcb_t *unblocked = removeBlocked(&devSems[semIdx]);
                if (unblocked != NULL) {
                    unblocked->p_s.reg_a0 = savedStatus;
                    unblocked->p_semAdd   = NULL;
                    insertProcQ(&readyQueue, unblocked);
                    softBlockCount--;
                }
            }
        }

        if (currentProcess != NULL) {
            LDST(savedState);
        } else {
            scheduler();
        }
        return;
    }

    /* ================================================================ */
    /* Unknown interrupt code: just resume/schedule                      */
    /* ================================================================ */
    if (currentProcess != NULL) LDST(savedState);
    else scheduler();
}