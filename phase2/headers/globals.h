    #ifndef GLOBALS_H
    #define GLOBALS_H

    #include "../../headers/types.h"
    #include "../../headers/const.h"
    #include <uriscv/liburiscv.h>

    /* -----------------------------------------------------------------------
    * Variabili globali del Nucleo (dichiarate in initial.c, usate ovunque)
    * ----------------------------------------------------------------------- */

    /* Numero di processi avviati ma non ancora terminati */
    extern int processCount;

    /* Numero di processi bloccati in attesa di I/O o timer (soft-block) */
    extern int softBlockCount;

    /* Coda dei processi pronti (Ready Queue) */
    extern struct list_head readyQueue;

    /* Puntatore al processo correntemente in esecuzione */
    extern pcb_t *currentProcess;

    /*
    * Lista globale di tutti i processi vivi (ready + blocked + running).
    * Ogni pcb_t viene aggiunto a questa lista tramite il campo p_list
    * al momento della creazione (CREATEPROCESS) e rimosso alla terminazione.
    *
    * Serve per permettere a TERMPROCESS di trovare un processo per PID
    * anche quando non è discendente di currentProcess (es. p10 termina p9).
    *
    * NOTA: p_list viene già usato dalla ready queue e dall'ASL, quindi
    * usiamo un campo dedicato. Aggiungiamo activeList come campo extra
    * — ma pcb_t non ha un campo libero apposito. La soluzione più pulita
    * senza modificare pcb_t è usare una lista separata con i pcb_t
    * collegati tramite p_list quando NON sono in ready queue né in ASL,
    * oppure mantenere un array parallelo di puntatori.
    *
    * Scelta adottata: array globale di puntatori pcb_t* activeProcs[MAXPROC].
    * - activeProcs[i] != NULL → processo vivo con quel slot occupato
    * - Aggiornato in CREATEPROCESS (inserimento) e terminateProcess (rimozione)
    */
    extern pcb_t *activeProcs[MAXPROC];

    /*
    * Array di semafori per i device esterni + pseudo-clock.
    * Indici [0..47]: semafori per i device (linee 3-7, 8 device per linea).
    *   indice = (IntLineNo - 3) * 8 + DevNo
    *   Per i terminali:
    *     TX: (IL_TERMINAL-3)*8 + DevNo  = 32+DevNo
    *     RX: (IL_TERMINAL-3)*8 + 8 + DevNo = 40+DevNo
    * Indice [48]: semaforo pseudo-clock
    */
    #define DEV_SEM_BASE(line, dev) (((line) - 3) * 8 + (dev))
    #define TERM_TX_SEM(dev)  (32 + (dev))
    #define TERM_RX_SEM(dev)  (40 + (dev))
    #define PSEUDOCLK_SEM           48
    #define TOT_SEMS                49

    extern int devSems[TOT_SEMS];

    /* TOD al momento del dispatch del processo corrente (per calcolare p_time) */
    extern cpu_t startTOD;

    #endif  