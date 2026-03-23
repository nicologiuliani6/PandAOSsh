/*
 * interrupts.c - Phase 2 (Nucleus Interrupt Handler)
 */

#include "../headers/const.h"
#include "../headers/types.h"
#include <uriscv/liburiscv.h>

#include "../phase1/headers/pcb.h"
#include "../phase1/headers/asl.h"
#include "./headers/globals.h"
#include "debug.h"

extern void scheduler(void);

#ifndef CAUSE_EXCCODE_MASK
#define CAUSE_EXCCODE_MASK 0xFFu
#endif

/* ================================================================ */
/* Interrupt debug                                                  */
/* ================================================================ */


#if DEBUG_INT
#define IDBG(msg)           debug_print(msg)
#define IDBG_HEX(msg,val)   debug_hex(msg,val)
#else
#define IDBG(msg)           ((void)0)
#define IDBG_HEX(msg,val)   ((void)0)
#endif

static void copyState(state_t *dst, state_t *src) {
    unsigned int *d = (unsigned int *) dst;
    unsigned int *s = (unsigned int *) src;
    for (int i = 0; i < STATE_T_SIZE_IN_BYTES / WORDLEN; i++) {
        d[i] = s[i];
    }
}

/* Interrupting Devices Bit Map */
#define INT_BITMAP_BASE  0x10000040
#define INT_BITMAP(line) (*((unsigned int *)(INT_BITMAP_BASE + ((line) - 3) * 0x4)))

/* Device register base address */
#define DEV_REG_BASE(line, dev) \
    ((unsigned int *)(START_DEVREG + ((line) - 3) * 0x80 + (dev) * 0x10))

#define DEV_STATUS(base)          ((base)[0])
#define DEV_COMMAND(base)         ((base)[1])

/* Terminal subdevices */
#define TERM_RECV_STATUS(base)    ((base)[0])
#define TERM_RECV_COMMAND(base)   ((base)[1])
#define TERM_TRANSM_STATUS(base)  ((base)[2])
#define TERM_TRANSM_COMMAND(base) ((base)[3])
/* helper per device con priorita maggiore*/
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

    /* CPU time accounting */
    cpu_t now;
    STCK(now);

    if (currentProcess != NULL) {
        currentProcess->p_time += (now - startTOD);
    }
    startTOD = now;

    IDBG("[INT] ===== Interrupt received =====\n");
    IDBG_HEX("[INT] CAUSE=", cause);
    IDBG_HEX("[INT] excCode=", excCode);

    /* PLT interrupt (excCode == 7)                                     */
    if (excCode == 7u) {

        /* Ack/disarm PLT */
        setTIMER((cpu_t) NEVER);

        if (currentProcess != NULL) {

            /* Salvo lo stato del processo al momento dell'interrupt */
            copyState(&currentProcess->p_s, savedState);

            /* Assicuro che, quando riparte, abbia gli interrupt abilitati */
            currentProcess->p_s.status |= MSTATUS_MIE_MASK;

            /* Round-robin: rimetto in ready queue */
            insertProcQ(&readyQueue, currentProcess);
            currentProcess = NULL;
        }

        scheduler();
        return;
    }

    /* Interval timer (pseudo-clock) (excCode == 3)                     */
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
            /* Nessun cambio di processo: riprende quello interrotto */
            LDST(savedState);
        } else {
            /* Nessun processo corrente: schedula qualcun altro */
            scheduler();
        }
        return;
    }

    /* Device interrupts: excCode 17..21            */
    if (excCode >= 17u && excCode <= 21u) {

        int intLineNo = (int)(excCode - 14u); 
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

            /* TX ha priorità su RX */
            if (txStatus != READY && txStatus != BUSY) {

                unsigned int savedStatus = TERM_TRANSM_STATUS(termBase);
                TERM_TRANSM_COMMAND(termBase) = ACK;

                int semIdx = TERM_TX_SEM(devNo);

                if (devSems[semIdx] < 0) {
                    devSems[semIdx]++;
                    pcb_t *unblocked = removeBlocked(&devSems[semIdx]);
                    if (unblocked != NULL) {
                        IDBG("[INT] unblocking device PID=");
                        IDBG_HEX("", (unsigned int) unblocked->p_pid);
                        IDBG(" semIdx=");
                        IDBG_HEX("", (unsigned int) semIdx);
                        IDBG(" softBlockCount=");
                        IDBG_HEX("", (unsigned int) softBlockCount);

                        unblocked->p_s.reg_a0 = savedStatus;
                        unblocked->p_semAdd   = NULL;
                        list_add_tail(&unblocked->p_list, &readyQueue);                          
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

    /* Unknown interrupt code: just resume/schedule                      */
    if (currentProcess != NULL) LDST(savedState);
    else scheduler();
}
