# Scelte implementative – Phase 1  
**Moduli `pcb.c` e `asl.c`**

## 1. Introduzione

Il presente documento descrive le principali **scelte implementative** adottate nei moduli `pcb.c` e `asl.c`, motivandole in relazione ai requisiti del progetto.  
Le scelte fatte mirano a garantire semplicità, efficienza e determismmo, evitando allocazioni dinamiche e usando strutture dati adeguate al contesto di un sistema operativo.

---

## 2. Scelte implementative in `pcb.c`

### 2.1 Rappresentazione dei PCB

Ogni processo è rappresentato tramite una struttura `pcb_t`.  
Si è scelto di organizzare i PCB utilizzando **liste doppiamente concatenate** basate su `struct list_head`, in quanto permettono inserimenti e rimozioni in tempo costante senza la necessità di ricopiare dati.

Questa scelta è particolarmente adatta a un sistema operativo, dove:
- i processi vengono frequentemente inseriti e rimossi da code;
- è necessario garantire efficienza e prevedibilità delle operazioni;
- non è desiderabile utilizzare strutture che richiedano riallocazioni.

---

### 2.2 Uso di una PCB Free List

I PCB sono preallocati in un array statico e gestiti tramite una **free list**.

Questa scelta è motivata da diversi fattori:
- evita l’uso di allocazioni dinamiche, che possono introdurre frammentazione e non determinismo;
- consente tempi di allocazione e deallocazione costanti;
- semplifica la gestione della memoria.

La free list permette di riutilizzare in modo efficiente i PCB non più in uso, mantenendo il controllo completo sulle risorse disponibili.

---

### 2.3 Gestione delle code di processi

Le code di processi sono implementate come **liste circolari** basate su `list_head`.

Questa scelta consente:
- inserimento e rimozione dei processi in tempo costante;
- accesso diretto alla testa della coda, utile per la gestione delle ready queue;
- un’implementazione uniforme delle primitive richieste dalle specifiche.

L’uso di liste circolari evita casi particolari nella gestione delle code vuote o con un solo elemento, semplificando il codice.

---

### 2.4 Gestione dell’albero dei processi

La relazione padre–figlio tra processi è implementata tramite:
- un puntatore al processo padre;
- una lista dei processi figli.

Questa struttura riflette direttamente la natura gerarchica dei processi in un sistema operativo.  
L’uso di liste per i figli consente di:
- gestire un numero arbitrario di processi figli;
- inserire e rimuovere figli in tempo costante;
- supportare facilmente operazioni di terminazione o rimozione di sottoalberi di processi.

---

## 3. Scelte implementative in `asl.c`

### 3.1 Struttura della Active Semaphore List (ASL)

L’ASL è implementata come una **lista di descrittori di semaforo** (`semd_t`), ciascuno associato a una specifica variabile semaforica.

Questa scelta permette di:
- mantenere separata la gestione dei semafori attivi da quelli inutilizzati;
- associare in modo diretto a ogni semaforo la coda dei processi bloccati;
- semplificare le operazioni di inserimento e rimozione dei processi bloccati.

La lista è mantenuta ordinata per indirizzo del semaforo per facilitare la ricerca del descrittore corretto.

---

### 3.2 Uso di una Semd Free List

Come per i PCB, anche i descrittori di semaforo sono preallocati in un array statico e gestiti tramite una **free list**.

Questa scelta:
- elimina la necessità di allocazioni dinamiche;
- garantisce un numero massimo noto di semafori gestibili;
- rende il comportamento del sistema più prevedibile.

I descrittori non più utilizzati vengono reinseriti nella free list per essere riutilizzati.

---

### 3.3 Blocco dei processi sui semafori

Quando un processo deve essere bloccato su un semaforo:
- si cerca il descrittore corrispondente nella ASL;
- se non esiste, ne viene allocato uno dalla free list;
- il processo viene inserito nella coda associata al semaforo;
- il PCB viene aggiornato con il riferimento al semaforo.

Questa soluzione consente di gestire in modo uniforme sia semafori già attivi sia semafori utilizzati per la prima volta.

---

### 3.4 Sblocco dei processi e rilascio dei semafori

Quando un processo viene rimosso dalla coda di un semaforo, si verifica se il semaforo ha ancora processi bloccati.

Se la coda diventa vuota:
- il descrittore viene rimosso dalla ASL;
- il descrittore viene reinserito nella free list.

Questa scelta evita di mantenere descrittori inutilizzati nella ASL e consente un uso efficiente delle risorse disponibili.

---

## 4. Considerazioni finali

Le scelte implementative sono state prese per garantire:
- semplicità delle strutture dati;
- efficienza delle operazioni fondamentali;
- comportamento deterministico;
- assenza di allocazioni dinamiche;
- aderenza alle specifiche del progetto.