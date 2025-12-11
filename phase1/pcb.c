#include "./headers/pcb.h"

static struct list_head pcbFree_h;
static pcb_t pcbFree_table[MAXPROC];
static int next_pid = 1;

/* Initialize "pcbFree_h" and add elements of "pcbFree_table" to the list "pcbFree_h"*/
void initPcbs() {
    int i;
    INIT_LIST_HEAD(&pcbFree_h);   // inizializza la lista come vuota

    for (i = 0; i < MAXPROC; i++) {
        INIT_LIST_HEAD(&pcbFree_table[i].p_list);   // inizializza il campo p_list
        pcbFree_table[i].p_pid = 0;                 // reset PID
        list_add_tail(&pcbFree_table[i].p_list, &pcbFree_h); // aggiungi alla lista dei PCB liberi
    }
}

/* Add PCB "p" to the list "pcbFree_h" */
void freePcb(pcb_t* p) {
    if (p != NULL) {
        list_add_tail(&p->p_list, &pcbFree_h); // reinserisci in coda alla lista dei liberi
    }
}

/* Allocate new PCB removing one from list "pcbFree_h" if possible */
pcb_t* allocPcb() {
    struct list_head* pos;
    pcb_t* new_pcb;

    if (list_empty(&pcbFree_h)) return NULL;  // nessun PCB disponibile

    // prendi il primo elemento dalla lista
    pos = pcbFree_h.next;
    list_del(pos);

    // ottieni il PCB corrispondente usando container_of
    new_pcb = container_of(pos, pcb_t, p_list);

    // inizializza i campi del PCB
    new_pcb->p_pid = next_pid++;
    INIT_LIST_HEAD(&new_pcb->p_list);   // resetta i puntatori della lista
    INIT_LIST_HEAD(&new_pcb->p_child);  // inizializza lista figli
    INIT_LIST_HEAD(&new_pcb->p_sib);    // inizializza lista fratelli
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

/* Insert PCB "p" in the list "head"*/
void insertProcQ(struct list_head* head, pcb_t* p) {
    struct list_head* pos;
    pcb_t* iter;

    // inserimento ordinato per priorità (p_prio più alto davanti)
    list_for_each(pos, head) {
        iter = container_of(pos, pcb_t, p_list);
        if (p->p_prio > iter->p_prio) {
            __list_add(&p->p_list, pos->prev, pos);
            return;
        }
    }
    // se non ha priorità maggiore, va in coda
    list_add_tail(&p->p_list, head);
}

/* Return the first PCB in the list "head" without removing it */
pcb_t* headProcQ(struct list_head* head) {
    if (list_empty(head)) return NULL;
    // head->next è il primo nodo della lista
    return container_of(head->next, pcb_t, p_list);
}

/* Remove and return the first PCB in the list "head" */
pcb_t* removeProcQ(struct list_head* head) {
    struct list_head* first;
    pcb_t* p;

    if (list_empty(head)) return NULL;

    first = head->next;
    list_del(first);
    p = container_of(first, pcb_t, p_list);
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
    return NULL; // PCB non trovato
}

/* Check if the PCB "p" has children */
int emptyChild(pcb_t* p) {
    return list_empty(&p->p_child);
}

/* Insert the PCB child "p" into the PCB parent "prnt" */
void insertChild(pcb_t *prnt, pcb_t *p) {
    p->p_parent = prnt; // Imposta il puntatore al padre
    INIT_LIST_HEAD(&p->p_list); // Inizializza la list_head del figlio
    list_add_tail(&p->p_list, &prnt->p_child); // Inserisce il figlio in coda
}

/* Remove and return the first child of the PCB "p" */
pcb_t* removeChild(pcb_t* p) {
    if (!p || list_empty(&p->p_child)) return NULL;  // Nessun figlio da rimuovere
    struct list_head *first = p->p_child.next; // Prende il primo elemento della lista dei figli
    pcb_t *child = container_of(first, pcb_t, p_list); // Ottiene il PCB
    list_del(&child->p_list); // Rimuove dalla lista dei figli
    child->p_parent = NULL; // Aggiorna il puntatore al padre
    INIT_LIST_HEAD(&child->p_list); // Inizializza p_list
    return child;
}

/* Remove and return the PCB "p" from its parent's children list */
pcb_t* outChild(pcb_t* p) {
    if (!p || !p->p_parent) return NULL;  // p non ha padre, non può essere rimosso
    list_del(&p->p_list); // Rimuove p dalla lista dei figli del padre
    p->p_parent = NULL; // Aggiorna il puntatore al padre
    INIT_LIST_HEAD(&p->p_list); // Inizializza p_list
    return p;
}

