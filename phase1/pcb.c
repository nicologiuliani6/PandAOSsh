#include "./headers/pcb.h"

static struct list_head pcbFree_h;
static pcb_t pcbFree_table[MAXPROC];
static int next_pid = 1;
static void debug_print4(const char *msg) {
    unsigned int *command = (unsigned int *)(0x10000254 + 3*4);
    
    while (*msg != '\0') {
        *command = 2 | (((unsigned int)*msg) << 8);
        /* delay */
        for (volatile int i = 0; i < 10000; i++);
        msg++;
    }
}
/* Initialize "pcbFree_h" and add elements of "pcbFree_table" to the list "pcbFree_h"*/
void initPcbs() {
    int i;
    INIT_LIST_HEAD(&pcbFree_h);

    for (i = 0; i < MAXPROC; i++) {
        INIT_LIST_HEAD(&pcbFree_table[i].p_list);
        pcbFree_table[i].p_pid = 0;
        list_add_tail(&pcbFree_table[i].p_list, &pcbFree_h);
    }
}

/* Add PCB "p" to the list "pcbFree_h" */
void freePcb(pcb_t* p) {
    if (p != NULL) list_add_tail(&p->p_list, &pcbFree_h);
}

/* Allocate new PCB removing one from list "pcbFree_h" if possible */
pcb_t* allocPcb() {
    struct list_head* pos;
    pcb_t* new_pcb;
    if (list_empty(&pcbFree_h)) return NULL;
    pos = pcbFree_h.next;
    list_del(pos);
    new_pcb = container_of(pos, pcb_t, p_list);
    new_pcb->p_pid = next_pid++;
    INIT_LIST_HEAD(&new_pcb->p_list);
    INIT_LIST_HEAD(&new_pcb->p_child);
    INIT_LIST_HEAD(&new_pcb->p_sib);
    new_pcb->p_parent = NULL;
    new_pcb->p_semAdd = NULL;
    new_pcb->p_supportStruct = NULL;
    new_pcb->p_time = 0;
    new_pcb->p_prio = 0;

    return new_pcb;
}

/* Create an empty PCB list */
void mkEmptyProcQ(struct list_head* head) {
    INIT_LIST_HEAD(head);
}

/* Check if the PCB list "head" is empty */
int emptyProcQ(struct list_head* head) {
    return list_empty(head);
}

/* Insert PCB "p" in the list "head" ordered by priority (highest first) */
void insertProcQ(struct list_head* head, pcb_t* p) {
    struct list_head* pos;
    pcb_t* iter;
    list_for_each(pos, head) {
        iter = container_of(pos, pcb_t, p_list);
        if (p->p_prio > iter->p_prio) {
            __list_add(&p->p_list, pos->prev, pos);
            return;
        }
    }
    list_add_tail(&p->p_list, head);
}

/* Return the first PCB in the list "head" without removing it */
pcb_t* headProcQ(struct list_head* head) {
    if (list_empty(head)) return NULL;
    return container_of(head->next, pcb_t, p_list);
}

/* Remove and return the first PCB in the list "head" */
pcb_t* removeProcQ(struct list_head* head) {
    debug_print4("\nentrato in removeProcQ");
    struct list_head* first;
    pcb_t* p;
    if (list_empty(head)) return NULL;
    first = head->next;
    list_del(first);
    INIT_LIST_HEAD(first);   // <-- aggiungere questo
    p = container_of(first, pcb_t, p_list);
    debug_print4("\nesco da removeProcQ\n");
    return p;
}   
/* Remove PCB "p" from the list "head" */
pcb_t* outProcQ(struct list_head* head, pcb_t* p) {
    struct list_head* pos;
    pcb_t* iter;
    list_for_each(pos, head) {
        iter = container_of(pos, pcb_t, p_list);
        if (iter == p) {
            list_del(pos);
            return p;
        }
    }
    return NULL;
}

/* Check if the PCB "p" has children */
int emptyChild(pcb_t* p) {
    return list_empty(&p->p_child);
}

/*
 * Insert the PCB child "p" into the PCB parent "prnt".
 * I figli sono collegati tramite p_sib (lista fratelli),
 * la testa della lista è p_child del padre.
 * p_list rimane libera per la Ready Queue / ASL.
 */
void insertChild(pcb_t *prnt, pcb_t *p) {
    p->p_parent = prnt;
    list_add_tail(&p->p_sib, &prnt->p_child);
}

/*
 * Remove and return the first child of the PCB "p".
 * Naviga tramite p_child → p_sib.
 */
pcb_t* removeChild(pcb_t* p) {
    if (!p || list_empty(&p->p_child)) return NULL;
    struct list_head *first = p->p_child.next;
    pcb_t *child = container_of(first, pcb_t, p_sib);
    list_del(&child->p_sib);
    INIT_LIST_HEAD(&child->p_sib);
    child->p_parent = NULL;
    return child;
}

/*
 * Remove and return the PCB "p" from its parent's children list.
 * Naviga tramite p_sib.
 */
pcb_t* outChild(pcb_t* p) {
    if (!p || !p->p_parent) return NULL;
    list_del(&p->p_sib);
    INIT_LIST_HEAD(&p->p_sib);
    p->p_parent = NULL;
    return p;
}