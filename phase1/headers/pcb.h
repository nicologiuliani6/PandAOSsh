// SPDX-FileCopyrightText: 2022 Luca Bassi, Gaia Clerici, Mirco Dondi, Fabio Gaiba
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef PCB_H_INCLUDED
#define PCB_H_INCLUDED

#include "../../headers/listx.h"
#include "../../headers/types.h"

// Initialize "pcbFree_h" and add elements of "pcbFree_table" to the list "pcbFree_h"
void initPcbs();

// Add PCB "p" to the list "pcbFree_h"
void freePcb(pcb_t* p);

// Allocate new PCB removing one from list "pcbFree_h" if possible
pcb_t* allocPcb();

// Create an empty PCB list
void mkEmptyProcQ(struct list_head* head);

// Check if the PCB list "head" is empty
int emptyProcQ(struct list_head* head);

// Insert PCB "p" in the list "head"
void insertProcQ(struct list_head* head, pcb_t* p);

// Return the first PCB in the list "head" without removing it
pcb_t* headProcQ(struct list_head* head);

// Remove and return the first PCB in the list "head"
pcb_t* removeProcQ(struct list_head* head);

// Remove PCB "p" from the list "head"
pcb_t* outProcQ(struct list_head* head, pcb_t* p);

// Check if the PCB "p" has children
int emptyChild(pcb_t* p);

// Insert the PCB child "p" into the PCB parent "prnt"
void insertChild(pcb_t* prnt, pcb_t* p);

// Remove and return the first child of the PCB "p"
pcb_t* removeChild(pcb_t* p);

// Remove and return the PCB "p" from the parent's children list
pcb_t* outChild(pcb_t* p);

#endif
