#include "../headers/const.h"
#include "../headers/types.h"
#include <uriscv/liburiscv.h>

#include "../phase1/headers/pcb.h"
#include "../phase1/headers/asl.h"
#include "./headers/globals.h"

extern void scheduler(void);

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

    debug_print("interruptHandler: ");
    debug_hex("mip=", mip);

    /* ------------------------------------------------------------------ */
    /* PLT (processor local timer / timeslice)                             */
    /* ------------------------------------------------------------------ */
    if (mip & MIP_BIT(IL_CPUTIMER)) {
        debug_hex("TIMESLICE=",     (unsigned int)TIMESLICE);
        debug_hex("TIMESCALE=",     (unsigned int)(*((cpu_t *)TIMESCALEADDR)));
        debug_hex("timer value=",   (unsigned int)(TIMESLICE * (*((cpu_t *)TIMESCALEADDR))));
        debug_print("-> PLT (timeslice expired)\n");
        debug_hex("PLT savedState->pc_epc=", savedState->pc_epc);
        debug_hex("PLT savedState->status=", savedState->status);
        debug_hex("PLT savedState->reg_sp=", savedState->reg_sp);
        debug_hex("PLT currentProcess=",     (unsigned int)currentProcess);

        setTIMER(TIMESLICE * (*((cpu_t *)TIMESCALEADDR)));

        if (currentProcess != NULL) {
            cpu_t now;
            STCK(now);
            currentProcess->p_time += (now - startTOD);
            copyState(&currentProcess->p_s, savedState);
            insertProcQ(&readyQueue, currentProcess);
            currentProcess = NULL;
        }
        scheduler();
        return;
    }

    /* ------------------------------------------------------------------ */
    /* Interval Timer (pseudo-clock tick ogni 100ms)                       */
    /* ------------------------------------------------------------------ */
    if (mip & MIP_BIT(IL_TIMER)) {
        debug_print("-> Interval Timer (pseudo-clock)\n");
        LDIT(PSECOND);

        pcb_t *unblocked;
        while ((unblocked = removeBlocked(&devSems[PSEUDOCLK_SEM])) != NULL) {
            unblocked->p_semAdd   = NULL;
            unblocked->p_s.reg_a0 = 0;
            insertProcQ(&readyQueue, unblocked);
            softBlockCount--;
        }
        devSems[PSEUDOCLK_SEM] = 0;

        if (currentProcess != NULL) {
            debug_print("IT: returning to currentProcess\n");
            LDST(savedState);
        } else {
            debug_print("IT: calling scheduler\n");
            scheduler();
        }
        return;
    }

    /* ------------------------------------------------------------------ */
    /* Device interrupt                                                     */
    /* ------------------------------------------------------------------ */
    debug_print("-> device interrupt\n");

    int intLineNo = -1;
    int devNo     = -1;
    int semIdx    = -1;

    for (int line = IL_DISK; line <= IL_TERMINAL; line++) {
        if (mip & MIP_BIT(line)) {
            unsigned int bitmap = INT_BITMAP(line);
            if (bitmap != 0) {
                intLineNo = line;
                devNo = getHighestPriorityDevice(bitmap);
                debug_hex("device line=", (unsigned int)intLineNo);
                debug_hex("device devNo=", (unsigned int)devNo);
                break;
            }
        }
    }

    if (intLineNo == -1 || devNo == -1) {
        debug_print("no device interrupt found, returning\n");
        if (currentProcess != NULL) LDST(savedState);
        else scheduler();
        return;
    }

    if (intLineNo == IL_TERMINAL) {
        unsigned int *termBase = DEV_REG_BASE(intLineNo, devNo);
        unsigned int  txStatus = TERM_TRANSM_STATUS(termBase) & 0xFF;
        unsigned int  rxStatus = TERM_RECV_STATUS(termBase)   & 0xFF;

        debug_hex("terminal txStatus=", txStatus);
        debug_hex("terminal rxStatus=", rxStatus);

        if (txStatus != READY && txStatus != BUSY) {
            debug_print("terminal TX interrupt\n");
            unsigned int savedStatus = TERM_TRANSM_STATUS(termBase);
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
            debug_print("terminal RX interrupt\n");
            unsigned int savedStatus = TERM_RECV_STATUS(termBase);
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
        } else {
            debug_print("terminal: no pending TX or RX\n");
        }

    } else {
        unsigned int *devBase     = DEV_REG_BASE(intLineNo, devNo);
        unsigned int  savedStatus = DEV_STATUS(devBase);
        debug_hex("device status=", savedStatus);
        DEV_COMMAND(devBase) = ACK;
        semIdx = DEV_SEM_BASE(intLineNo, devNo);
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

    if (currentProcess != NULL) {
        debug_print("device: returning to currentProcess\n");
        LDST(savedState);
    } else {
        debug_print("device: calling scheduler\n");
        scheduler();
    }
}