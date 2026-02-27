#include "../headers/const.h"
#include "../headers/types.h"
#include <uriscv/liburiscv.h>

#include "../phase1/headers/pcb.h"
#include "../phase1/headers/asl.h"
#include "./headers/globals.h"

extern void scheduler(void);

#include "debug.h"

static void copyState(state_t *dst, state_t *src) {
    unsigned int *d = (unsigned int *) dst;
    unsigned int *s = (unsigned int *) src;
    for (int i = 0; i < STATESIZE / WORDLEN; i++)
        d[i] = s[i];
}

#define INT_BITMAP_BASE  0x10000040
#define INT_BITMAP(line) (*((unsigned int *)(INT_BITMAP_BASE + ((line) - 3) * 0x4)))

#define DEV_REG_BASE(line, dev) \
    ((unsigned int *)(START_DEVREG + ((line) - 3) * 0x80 + (dev) * 0x10))

#define DEV_STATUS(base)   ((base)[0])
#define DEV_COMMAND(base)  ((base)[1])

#define TERM_RECV_STATUS(base)    ((base)[0])
#define TERM_RECV_COMMAND(base)   ((base)[1])
#define TERM_TRANSM_STATUS(base)  ((base)[2])
#define TERM_TRANSM_COMMAND(base) ((base)[3])

#define MIP_BIT(il_no) (1u << (il_no))

static int getHighestPriorityDevice(unsigned int bitmap) {
    for (int i = 0; i < 8; i++) {
        if (bitmap & (1 << i)) return i;
    }
    return -1;
}

void interruptHandler(void) {

    state_t     *savedState = (state_t *) BIOSDATAPAGE;
    unsigned int mip        = getMIP();

    debug_print("\n[INT] ===== Interrupt received =====\n");
    debug_hex("[INT] MIP=", mip);

    /* ================================================================ */
    /* PLT - Processor Local Timer (timeslice)                         */
    /* ================================================================ */
    if (mip & MIP_BIT(IL_CPUTIMER)) {

        debug_print("[PLT] Timeslice expired\n");

        debug_hex("[PLT] currentProcess=", (unsigned int)currentProcess);
        debug_hex("[PLT] pc_epc=", savedState->pc_epc);
        debug_hex("[PLT] status=", savedState->status);

        setTIMER(TIMESLICE * (*((cpu_t *)TIMESCALEADDR)));

        if (currentProcess != NULL) {

            cpu_t now;
            STCK(now);

            debug_hex("[PLT] startTOD=", startTOD);
            debug_hex("[PLT] now=", now);

            currentProcess->p_time += (now - startTOD);

            copyState(&currentProcess->p_s, savedState);

            debug_print("[PLT] Reinserting process in ReadyQueue\n");

            insertProcQ(&readyQueue, currentProcess);
            currentProcess = NULL;
        }

        debug_print("[PLT] Calling scheduler\n");
        scheduler();
        return;
    }

    /* ================================================================ */
    /* Interval Timer - Pseudo Clock                                    */
    /* ================================================================ */
    if (mip & MIP_BIT(IL_TIMER)) {

        debug_print("[IT] Pseudo-clock tick (100ms)\n");

        LDIT(PSECOND);

        pcb_t *unblocked;
        int count = 0;

        while ((unblocked = removeBlocked(&devSems[PSEUDOCLK_SEM])) != NULL) {

            debug_print("[IT] Unblocking process from pseudo-clock\n");

            unblocked->p_semAdd   = NULL;
            unblocked->p_s.reg_a0 = 0;

            insertProcQ(&readyQueue, unblocked);
            softBlockCount--;
            count++;
        }

        debug_hex("[IT] Processes unblocked=", count);

        devSems[PSEUDOCLK_SEM] = 0;

        if (currentProcess != NULL) {
            debug_print("[IT] Returning to currentProcess\n");
            LDST(savedState);
        } else {
            debug_print("[IT] No running process -> scheduler\n");
            scheduler();
        }

        return;
    }

    /* ================================================================ */
    /* Device Interrupt                                                  */
    /* ================================================================ */

    debug_print("[DEV] Device interrupt detected\n");

    int intLineNo = -1;
    int devNo     = -1;
    int semIdx    = -1;

    for (int line = IL_DISK; line <= IL_TERMINAL; line++) {

        if (mip & MIP_BIT(line)) {

            unsigned int bitmap = INT_BITMAP(line);

            if (bitmap != 0) {

                intLineNo = line;
                devNo     = getHighestPriorityDevice(bitmap);

                debug_hex("[DEV] Line=", intLineNo);
                debug_hex("[DEV] Device=", devNo);

                break;
            }
        }
    }

    if (intLineNo == -1 || devNo == -1) {

        debug_print("[DEV] Spurious interrupt (no device found)\n");

        if (currentProcess != NULL) LDST(savedState);
        else scheduler();

        return;
    }

    /* ================= Terminal ================= */

    if (intLineNo == IL_TERMINAL) {

        unsigned int *termBase = DEV_REG_BASE(intLineNo, devNo);

        unsigned int txStatus = TERM_TRANSM_STATUS(termBase) & 0xFF;
        unsigned int rxStatus = TERM_RECV_STATUS(termBase)   & 0xFF;

        debug_hex("[TERM] TX status=", txStatus);
        debug_hex("[TERM] RX status=", rxStatus);

        if (txStatus != READY && txStatus != BUSY) {

            debug_print("[TERM] TX completed\n");

            unsigned int savedStatus = TERM_TRANSM_STATUS(termBase);
            TERM_TRANSM_COMMAND(termBase) = ACK;

            semIdx = TERM_TX_SEM(devNo);

            debug_hex("[TERM] semIdx=", semIdx);

            devSems[semIdx]++;

            if (devSems[semIdx] <= 0) {

                pcb_t *unblocked = removeBlocked(&devSems[semIdx]);

                if (unblocked != NULL) {

                    debug_print("[TERM] Unblocking TX process\n");

                    unblocked->p_s.reg_a0 = savedStatus;
                    unblocked->p_semAdd   = NULL;

                    insertProcQ(&readyQueue, unblocked);
                    softBlockCount--;
                }
            }

        } else if (rxStatus != READY && rxStatus != BUSY) {

            debug_print("[TERM] RX completed\n");

            unsigned int savedStatus = TERM_RECV_STATUS(termBase);
            TERM_RECV_COMMAND(termBase) = ACK;

            semIdx = TERM_RX_SEM(devNo);

            debug_hex("[TERM] semIdx=", semIdx);

            devSems[semIdx]++;

            if (devSems[semIdx] <= 0) {

                pcb_t *unblocked = removeBlocked(&devSems[semIdx]);

                if (unblocked != NULL) {

                    debug_print("[TERM] Unblocking RX process\n");

                    unblocked->p_s.reg_a0 = savedStatus;
                    unblocked->p_semAdd   = NULL;

                    insertProcQ(&readyQueue, unblocked);
                    softBlockCount--;
                }
            }

        } else {

            debug_print("[TERM] No valid TX/RX cause\n");
        }

    }
    /* ================= Other devices ================= */

    else {

        unsigned int *devBase     = DEV_REG_BASE(intLineNo, devNo);
        unsigned int  savedStatus = DEV_STATUS(devBase);

        debug_hex("[DEV] Status=", savedStatus);

        DEV_COMMAND(devBase) = ACK;

        semIdx = DEV_SEM_BASE(intLineNo, devNo);

        debug_hex("[DEV] semIdx=", semIdx);

        devSems[semIdx]++;

        if (devSems[semIdx] <= 0) {

            pcb_t *unblocked = removeBlocked(&devSems[semIdx]);

            if (unblocked != NULL) {

                debug_print("[DEV] Unblocking process\n");

                unblocked->p_s.reg_a0 = savedStatus;
                unblocked->p_semAdd   = NULL;

                insertProcQ(&readyQueue, unblocked);
                softBlockCount--;
            }
        }
    }

    /* ================================================================ */

    if (currentProcess != NULL) {

        debug_print("[INT] Returning to running process\n");
        LDST(savedState);

    } else {

        debug_print("[INT] No running process -> scheduler\n");
        scheduler();
    }
}
