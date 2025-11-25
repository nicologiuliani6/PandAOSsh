// SPDX-FileCopyrightText: 2022 Luca Bassi, Gaia Clerici, Mirco Dondi, Fabio Gaiba
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef ASL_H_INCLUDED
#define ASL_H_INCLUDED

#include "../../headers/listx.h"
#include "../../headers/types.h"

// Initialize the lists "semdFree_h" and "semd_h" and add all the elements of "semd_table" to "semdFree_h"
void initASL();

// Add the PCB "p" to the semaphore with key "semAdd"
int insertBlocked(int* semAdd, pcb_t* p);

// Remove the first PCB from the semaphore with key "semAdd"
pcb_t* removeBlocked(int* semAdd);

// Remove PCB "p" from its semaphore
pcb_t* outBlocked(pcb_t* p);

// Return the first blocked PCB of the semaphore with key "semAdd"
pcb_t* headBlocked(int* semAdd);

#endif
