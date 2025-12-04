#include "./headers/pcb.h"

static struct list_head pcbFree_h;
static pcb_t pcbFree_table[MAXPROC];
static int next_pid = 1;

void initPcbs() {
    INIT_LIST_HEAD(&pcbFree_h);

    for (int i = 0; i < MAXPROC; i++) {
        freePcb(&pcbFree_table[i]);     // reset completo
        pcbFree_table[i].p_pid = next_pid++;
        list_add_tail(&pcbFree_table[i].p_list, &pcbFree_h);
    }
}


void freePcb(pcb_t* p) {
    p->p_parent = NULL;
    INIT_LIST_HEAD(&p->p_child);
    INIT_LIST_HEAD(&p->p_sib);
    p->p_semAdd = NULL;
    p->p_supportStruct = NULL;
    p->p_prio = 0;
    p->p_time = 0;
    p->p_s.entry_hi = 0;
    p->p_s.cause = 0;
    p->p_s.status = 0;
    p->p_s.pc_epc = 0;
    p->p_s.mie = 0;
    for (int i = 0; i < STATE_GPR_LEN; i++) {
        p->p_s.gpr[i] = 0;
    }
    p->p_pid = 0;
}

pcb_t* allocPcb() {
    
}

void mkEmptyProcQ(struct list_head* head) {
}

int emptyProcQ(struct list_head* head) {
}

void insertProcQ(struct list_head* head, pcb_t* p) {
}

pcb_t* headProcQ(struct list_head* head) {
}

pcb_t* removeProcQ(struct list_head* head) {
}

pcb_t* outProcQ(struct list_head* head, pcb_t* p) {
}

int emptyChild(pcb_t* p) {
}

void insertChild(pcb_t* prnt, pcb_t* p) {
}

pcb_t* removeChild(pcb_t* p) {
}

pcb_t* outChild(pcb_t* p) {
}
