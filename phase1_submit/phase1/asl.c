#include "./headers/asl.h"

/* Liste globali dei SEMD */
static semd_t semd_table[MAXPROC];
static struct list_head semdFree_h;
static struct list_head semd_h;

/* Initialize the lists "semdFree_h" and "semd_h" and add all the elements of "semd_table" to "semdFree_h" */
void initASL() {
    INIT_LIST_HEAD(&semdFree_h);
    INIT_LIST_HEAD(&semd_h);
    for (int i = 0; i < MAXPROC; i++) {
        semd_table[i].s_key = NULL;
        INIT_LIST_HEAD(&semd_table[i].s_procq);
        list_add_tail(&semd_table[i].s_link, &semdFree_h);
    }
}

/* Add the PCB "p" to the semaphore with key "semAdd" */
int insertBlocked(int* semAdd, pcb_t* p) {
    if (!semAdd || !p) return 1; // errore
    semd_t* s;
    list_for_each_entry(s, &semd_h, s_link) {
        if (s->s_key == semAdd) {
            list_add_tail(&p->p_list, &s->s_procq);
            p->p_semAdd = semAdd;

            return 0;
        }
    }
    // semd non trovato, usa uno dalla free list
    if (list_empty(&semdFree_h)) return 1; // nessun semd disponibile
    s = container_of(semdFree_h.next, semd_t, s_link);
    list_del(&s->s_link);
    s->s_key = semAdd;
    INIT_LIST_HEAD(&s->s_procq);
    list_add_tail(&p->p_list, &s->s_procq);
    p->p_semAdd = semAdd;
    list_add_tail(&s->s_link, &semd_h);

    return 0;
}

/* Remove the first PCB from the semaphore with key "semAdd" */
pcb_t* removeBlocked(int* semAdd) {
    if (!semAdd) return NULL;
    semd_t* s;
    list_for_each_entry(s, &semd_h, s_link) {
        if (s->s_key == semAdd) {
            if (list_empty(&s->s_procq)) return NULL;
            pcb_t* p = container_of(s->s_procq.next, pcb_t, p_list);
            list_del(&p->p_list);
            p->p_semAdd = NULL;
            // se la coda diventa vuota, libera il semd
            if (list_empty(&s->s_procq)) {
                list_del(&s->s_link);
                s->s_key = NULL;
                list_add_tail(&s->s_link, &semdFree_h);
            }

            return p;
        }
    }
    return NULL;
}

/* Remove PCB "p" from its semaphore */
pcb_t* outBlocked(pcb_t* p) {
    if (!p || !p->p_semAdd) return NULL;
    semd_t* s;
    list_for_each_entry(s, &semd_h, s_link) {
        if (s->s_key == p->p_semAdd) {
            list_del(&p->p_list);
            p->p_semAdd = NULL;
            // se la coda diventa vuota, libera il semd
            if (list_empty(&s->s_procq)) {
                list_del(&s->s_link);
                s->s_key = NULL;
                list_add_tail(&s->s_link, &semdFree_h);
            }

            return p;
        }
    }

    return NULL;
}

/* Return the first blocked PCB of the semaphore with key "semAdd" */
pcb_t* headBlocked(int* semAdd) {
    if (!semAdd) return NULL;
    semd_t* s;
    list_for_each_entry(s, &semd_h, s_link) {
        if (s->s_key == semAdd) {
            if (list_empty(&s->s_procq)) return NULL;

            return container_of(s->s_procq.next, pcb_t, p_list);
        }
    }

    return NULL;
}