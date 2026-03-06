#include <stdio.h>
#include "headers/asl.h"

int main() {
    pcb_t p1, p2, p3, p4;

    int sem1 = 100;
    int sem2 = 200;
    int sem3 = 300;

    // Inizializza ASL
    initASL();

    // Inizializza le liste dei PCB
    INIT_LIST_HEAD(&p1.p_list);
    INIT_LIST_HEAD(&p2.p_list);
    INIT_LIST_HEAD(&p3.p_list);
    INIT_LIST_HEAD(&p4.p_list);

    // Inserisci PCB nei semafori
    insertBlocked(&sem1, &p1);
    insertBlocked(&sem1, &p2);
    insertBlocked(&sem2, &p3);
    insertBlocked(&sem2, &p4);

    printf("Dopo insert:\n=== ASL state ===\n");

    // Stampa lo stato dei semafori
    pcb_t* p;
    for (int* s = &sem1; s <= &sem3; s += 100) {
        printf("Semaforo %d: ", *s);
        if ((p = headBlocked(s)) == NULL) {
            printf("vuoto\n");
        } else {
            pcb_t* tmp = p;
            do {
                printf("%p ", (void*)tmp);
                tmp = (tmp->p_list.next != &tmp->p_list) ? 
                        container_of(tmp->p_list.next, pcb_t, p_list) : NULL;
            } while (tmp != NULL && tmp != p);
            printf("\n");
        }
    }
    printf("=================\n");

    // Rimuovi da sem1
    p = removeBlocked(&sem1);
    printf("Rimosso da sem1: %p\n", (void*)p);

    printf("Dopo removeBlocked sem1:\n=== ASL state ===\n");
    for (int* s = &sem1; s <= &sem3; s += 100) {
        printf("Semaforo %d: ", *s);
        if ((p = headBlocked(s)) == NULL) printf("vuoto\n");
        else printf("%p\n", (void*)p);
    }
    printf("=================\n");

    // Rimuovi p4 da sem2
    p = outBlocked(&p4);
    printf("Dopo outBlocked p4 da sem2:\n=== ASL state ===\n");
    for (int* s = &sem1; s <= &sem3; s += 100) {
        printf("Semaforo %d: ", *s);
        if ((p = headBlocked(s)) == NULL) printf("vuoto\n");
        else printf("%p\n", (void*)p);
    }
    printf("=================\n");

    return 0;
}