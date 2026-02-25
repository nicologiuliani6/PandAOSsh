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
 * Array di semafori per i device esterni + pseudo-clock.
 * Indici [0..47]: semafori per i device (linee 3-7, 8 device per linea).
 *   indice = (IntLineNo - 3) * 8 + DevNo
 *   Per i terminali:
 *     TX: indice base della linea 7 + DevNo         => (7-3)*8 + DevNo = 32+DevNo
 *     RX: indice base della linea 7 + 8 + DevNo     => 40+DevNo
 * Indice [48]: semaforo pseudo-clock
 */
#define DEV_SEM_BASE(line, dev) (((line) - 3) * 8 + (dev))
#define TERM_TX_SEM(dev)        (DEV_SEM_BASE(IL_TERMINAL, (dev)))
#define TERM_RX_SEM(dev)        (DEV_SEM_BASE(IL_TERMINAL, (dev)) + 8)
#define PSEUDOCLK_SEM           48
#define TOT_SEMS                49

extern int devSems[TOT_SEMS];

/* TOD al momento del dispatch del processo corrente (per calcolare p_time) */
extern cpu_t startTOD;

#endif
