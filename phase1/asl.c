#include "./headers/asl.h"

static semd_t semd_table[MAXPROC];
static struct list_head semdFree_h;
static struct list_head semd_h;

void initASL() {
}

int insertBlocked(int* semAdd, pcb_t* p) {
}

pcb_t* removeBlocked(int* semAdd) {
}

pcb_t* outBlocked(pcb_t* p) {
}

pcb_t* headBlocked(int* semAdd) {
}
