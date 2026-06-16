#ifndef PANDOS_SUPPORT_H
#define PANDOS_SUPPORT_H

/****************************************************************************
 * Phase 3 / Level 4 - The Support Level
 *
 * Dichiarazioni condivise tra initProc.c, vmSupport.c e sysSupport.c:
 *   - variabili globali del Support Level
 *   - costanti e macro di supporto (Swap Pool, indirizzi kuseg, device)
 *   - prototipi delle funzioni pubbliche
 ****************************************************************************/

#include "../../headers/const.h"
#include "../../headers/types.h"
#include <uriscv/liburiscv.h>
#include <uriscv/arch.h>
#include <uriscv/cpu.h>

/* Costanti del Support Level*/

/* Inizio dello Swap Pool: subito dopo i primi 32 frame riservati al SO
 * (0x20000000 + 32 * 4096 = 0x20020000). */
#define SWAP_POOL_START (RAMSTART + (OSFRAMES * PAGESIZE))

/* Numero di frame nello Swap Pool: 2 * UPROCMAX (POOLSIZE = 16). */
#define SWAP_POOL_SIZE  POOLSIZE

/* Indirizzamento logico kuseg di una U-proc. */
#define KUSEG_VPN_START   0x80000   /* VPN della prima pagina (0x80000000) */
#define KUSEG_STACK_VPN   0xBFFFF   /* VPN della pagina di stack            */
#define UPROC_STACKPAGE   31        /* indice della pagina di stack in PgTbl */
#define UPROC_TEXTPAGES   31        /* pagine 0..30 per .text/.data          */

/* Frame "vuoto" nella Swap Pool table (ASID non valido). */
#define SWAP_FRAME_FREE  (-1)

/* Stato processore per le U-proc: user-mode, interrupt e PLT abilitati. */
#define UPROC_STATUS  (MSTATUS_MPIE_MASK | MSTATUS_MPP_U)
/* Stato per i context degli handler del Support Level: kernel-mode,
 * interrupt e PLT abilitati. */
#define SUPPORT_STATUS (MSTATUS_MIE_MASK | MSTATUS_MPIE_MASK | MSTATUS_MPP_M)

/* Codici di eccezione (indici in sup_exceptState / sup_exceptContext). */
/* PGFAULTEXCEPT = 0, GENERALEXCEPT = 1 (già in const.h). */

/* Numeri delle SYSCALL del Support Level (a0 positivo). */
#define SUP_TERMINATE      2
#define SUP_WRITEPRINTER   3
#define SUP_WRITETERMINAL  4
#define SUP_READTERMINAL   5
#define SUP_EXECUTE        6

/* Lunghezza massima di una stringa scrivibile su terminale (SYS4). */
#define MAXSTRLEN 128

/* Semafori di mutua esclusione sui device (uno per sotto-device)      */
/* Layout: [0..39] i 5*8 device "principali", [40..47] il secondo      */
/* sotto-device dei terminali (ricezione).                             */
#define DEVCNT            (DEVINTNUM * DEVPERINT)        /* 40 */
#define DEV_MUTEX_TOTAL   (DEVCNT + DEVPERINT)           /* 48 */

#define FLASH_MUTEX(dev)    (((IL_FLASH - IL_DISK) * DEVPERINT) + (dev))
#define PRINTER_MUTEX(dev)  (((IL_PRINTER - IL_DISK) * DEVPERINT) + (dev))
#define TERMW_MUTEX(dev)    (((IL_TERMINAL - IL_DISK) * DEVPERINT) + (dev))
#define TERMR_MUTEX(dev)    (DEVCNT + (dev))

/* Variabili globali del Support Level                                 */

/* Semaforo di mutua esclusione sullo Swap Pool (init 1). */
extern int swapPoolSem;
/* Swap Pool table: una entry per frame. */
extern swap_t swapPool[SWAP_POOL_SIZE];

/* Semaforo per la conclusione "graziosa" dell'InstantiatorProcess. */
extern int masterSemaphore;
/* Semaforo su cui la shell si blocca durante una SYS6. */
extern int shellSemaphore;

/* Semafori di mutua esclusione sui device. */
extern int devMutex[DEV_MUTEX_TOTAL];

/* Pool delle support structure (una per U-proc, indicizzata da ASID-1). */
extern support_t supportPool[UPROCMAX];

/* Prototipi                                                           */

/* initProc.c */
extern void test(void);                 /* InstantiatorProcess */
extern void launchUproc(int asid);      /* inizializza e avvia una U-proc */
extern support_t *getSupport(int asid);

/* vmSupport.c */
extern void initSwapStructs(void);      /* inizializza Swap Pool + semaforo */
extern void pager(void);                /* TLB exception handler (Pager) */
extern void initUprocPageTable(support_t *sup);
extern int  flashOperation(int asid, int blockNo, memaddr frameAddr, int op);

/* sysSupport.c */
extern void generalExceptionHandler(void); /* GENERALEXCEPT handler */
extern void supTerminate(int asid);        /* terminazione ordinata di U-proc */

#endif /* PANDOS_SUPPORT_H */
