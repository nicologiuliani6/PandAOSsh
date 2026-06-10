/*
 * sysSupport.c - Phase 3 / Level 4
 *
 * Handler del Support Level per le eccezioni "non-TLB" passate su dal
 * Nucleus:
 *   - General Exception Handler (smista syscall e program trap)
 *   - SYSCALL Handler (SYS2 Terminate, SYS4 Write, SYS5 Read, SYS6 Execute)
 *   - Program Trap Handler (terminazione ordinata)
 */

#include "headers/support.h"

/* ------------------------------------------------------------------ */
/* Terminazione ordinata di una U-proc                                 */
/* ------------------------------------------------------------------ */

void supTerminate(int asid) {
    /* Libera i frame dello Swap Pool occupati da questa U-proc, così da
     * evitare scritture spurie sul backing store in futuro. */
    SYSCALL(PASSEREN, (int)&swapPoolSem, 0, 0);
    for (int i = 0; i < SWAP_POOL_SIZE; i++)
        if (swapPool[i].sw_asid == asid)
            swapPool[i].sw_asid = SWAP_FRAME_FREE;
    SYSCALL(VERHOGEN, (int)&swapPoolSem, 0, 0);

    /* Sblocca chi attende la conclusione di questa U-proc:
     *  - la shell (ASID 1)  -> InstantiatorProcess via masterSemaphore
     *  - una U-proc figlia  -> la shell via shellSemaphore                */
    if (asid == 1)
        SYSCALL(VERHOGEN, (int)&masterSemaphore, 0, 0);
    else
        SYSCALL(VERHOGEN, (int)&shellSemaphore, 0, 0);

    /* Terminazione effettiva tramite il Nucleus (NSYS2). */
    SYSCALL(TERMPROCESS, 0, 0, 0);
}

/* ------------------------------------------------------------------ */
/* SYS4 - WriteTerminal                                                */
/* ------------------------------------------------------------------ */

static int writeTerminal(support_t *sup, char *virtAddr, int len) {
    /* Validazione: indirizzo dentro lo spazio logico, lunghezza valida. */
    if ((memaddr)virtAddr < KUSEG || (memaddr)virtAddr >= USERSTACKTOP ||
        len < 0 || len > MAXSTRLEN) {
        supTerminate(sup->sup_asid); /* non ritorna */
    }

    termreg_t *term  = (termreg_t *) DEV_REG_ADDR(IL_TERMINAL, 0);
    int        mutex = TERMW_MUTEX(0);

    SYSCALL(PASSEREN, (int)&devMutex[mutex], 0, 0);

    int i;
    for (i = 0; i < len; i++) {
        unsigned int cmd =
            (((unsigned int)(unsigned char)virtAddr[i]) << 8) | TRANSMITCHAR;
        unsigned int status =
            SYSCALL(DOIO, (int)&term->transm_command, (int)cmd, 0);
        if ((status & 0xFF) != OKCHARTRANS) {
            SYSCALL(VERHOGEN, (int)&devMutex[mutex], 0, 0);
            return -(int)(status & 0xFF);
        }
    }

    SYSCALL(VERHOGEN, (int)&devMutex[mutex], 0, 0);
    return i;
}

/* ------------------------------------------------------------------ */
/* SYS5 - ReadTerminal                                                 */
/* ------------------------------------------------------------------ */

static int readTerminal(support_t *sup, char *virtAddr) {
    if ((memaddr)virtAddr < KUSEG || (memaddr)virtAddr >= USERSTACKTOP) {
        supTerminate(sup->sup_asid); /* non ritorna */
    }

    termreg_t *term  = (termreg_t *) DEV_REG_ADDR(IL_TERMINAL, 0);
    int        mutex = TERMR_MUTEX(0);

    SYSCALL(PASSEREN, (int)&devMutex[mutex], 0, 0);

    int count = 0;
    int done  = 0;
    while (!done) {
        unsigned int status =
            SYSCALL(DOIO, (int)&term->recv_command, (int)RECEIVECHAR, 0);
        if ((status & 0xFF) != CHARRECV) {
            SYSCALL(VERHOGEN, (int)&devMutex[mutex], 0, 0);
            return -(int)(status & 0xFF);
        }
        char c = (char)((status >> 8) & 0xFF);
        virtAddr[count++] = c;
        if (c == '\n')
            done = 1;
    }

    SYSCALL(VERHOGEN, (int)&devMutex[mutex], 0, 0);
    return count;
}

/* ------------------------------------------------------------------ */
/* SYS6 - Execute (spawn di una nuova U-proc; usato dalla shell)        */
/* ------------------------------------------------------------------ */

static int doExecute(int asid) {
    if (asid < 1 || asid > UPROCMAX)
        return -1;
    launchUproc(asid);
    /* La shell si blocca finché la U-proc figlia non termina. */
    SYSCALL(PASSEREN, (int)&shellSemaphore, 0, 0);
    return 0;
}

/* ------------------------------------------------------------------ */
/* SYSCALL Handler                                                     */
/* ------------------------------------------------------------------ */

static void supSyscallHandler(support_t *sup, state_t *state) {
    int number = (int)state->reg_a0;
    int result = 0;

    switch (number) {
        case SUP_TERMINATE:
            supTerminate(sup->sup_asid); /* non ritorna */
            return;

        case SUP_WRITETERMINAL:
            result = writeTerminal(sup, (char *)state->reg_a1,
                                   (int)state->reg_a2);
            break;

        case SUP_READTERMINAL:
            result = readTerminal(sup, (char *)state->reg_a1);
            break;

        case SUP_EXECUTE:
            result = doExecute((int)state->reg_a1);
            break;

        default:
            /* SYSCALL non riconosciuta: trattata come program trap. */
            supTerminate(sup->sup_asid); /* non ritorna */
            return;
    }

    /* Ritorno alla U-proc: valore in a0 e PC avanzato oltre la ecall. */
    state->reg_a0  = (unsigned int)result;
    state->pc_epc += WORDLEN;
    LDST(state);
}

/* ------------------------------------------------------------------ */
/* General Exception Handler                                           */
/* ------------------------------------------------------------------ */

void generalExceptionHandler(void) {
    support_t *sup   = (support_t *) SYSCALL(GETSUPPORTPTR, 0, 0, 0);
    state_t   *state = &sup->sup_exceptState[GENERALEXCEPT];

    unsigned int excCode = state->cause & CAUSE_EXCCODE_MASK;

    if (excCode == EXC_ECU) {
        /* ECALL da user-mode: è una SYSCALL del Support Level. */
        supSyscallHandler(sup, state);
    } else {
        /* Qualsiasi altra eccezione: program trap -> terminazione. */
        supTerminate(sup->sup_asid);
    }
}
