# Scelte implementative – Phase 2  
**Moduli `initial.c` · `scheduler.c` · `exceptions.c` · `interrupts.c`**

---

## 1. Introduzione

Il presente documento descrive le principali **scelte implementative** adottate nei moduli della Phase 2 del nucleo (Nucleus), motivandole in relazione ai requisiti del progetto e alle specifiche µRISCV. Dove il codice si discosta dalla specifica, la deviazione è segnalata esplicitamente e giustificata.

La Phase 2 realizza il kernel vero e proprio: inizializzazione del sistema, scheduling dei processi, gestione delle eccezioni (syscall, trap, TLB) e gestione degli interrupt. Tutte le scelte mirano a garantire correttezza, semplicità e comportamento deterministico, operando al di sotto del livello utente senza alcuna allocazione dinamica.

---

## 2. Modulo `initial.c` – Inizializzazione del sistema

### 2.1 Configurazione del Pass Up Vector

Questa scelta è stata adottata dopo aver riscontrato collisioni di stack durante i test: con un unico indirizzo fisso (`KERNELSTACK`) i due gestori potevano sovrascriversi reciprocamente in caso di eccezioni annidate. Usando `ramtop` e `ramtop − PAGESIZE` si riservano i due frame più alti della RAM esclusivamente agli handler del kernel, garantendo isolamento. Il processo di test usa `ramtop − 2 × PAGESIZE` come stack, evitando sovrapposizioni con entrambi i frame riservati.

### 2.2 Lettura del TOD invece dell'azzeramento

La variabile globale `startTOD` viene inizializzata con una lettura reale del TOD hardware (`STCK`), anziché azzerata. Questo approccio evita che il primo processo accumuli artificialmente tempo CPU durante la fase di bootstrap, garantendo misurazioni corrette fin dalla prima esecuzione.

### 2.3 Strutture globali e semafori dispositivo

Tutte le variabili globali (`processCount`, `softBlockCount`, `readyQueue`, `currentProcess`, `devSems`, `activeProcs`) sono dichiarate in `initial.c` e condivise tramite `extern` negli altri moduli. I semafori dei dispositivi vengono inizializzati a zero in un ciclo unico, adottando la semantica a contatore negativo.

### 2.4 Processo iniziale e avvio dello scheduler

Viene creato un singolo processo `test` con priorità bassa (`PROCESS_PRIO_LOW`), inserito nella ready queue, e lo scheduler viene invocato direttamente. Questa scelta mantiene la logica di selezione del processo interamente nello scheduler, evitando duplicazioni di codice in `main`.

### 2.5 Tracciamento dei processi attivi (`activeProcs`)

> **Scelta progettuale aggiuntiva rispetto alla specifica**  
> La specifica non richiede esplicitamente questa struttura. Il suo utilizzo introduce un overhead O(MAXPROC) nelle operazioni di ricerca, inserimento e rimozione, accettabile dato il limite fisso di processi.

Accanto alla ready queue, viene mantenuto un array `activeProcs` di dimensione `MAXPROC` che contiene i puntatori a tutti i processi vivi (ready, running o bloccati). Questa struttura è necessaria per due operazioni che la sola ready queue non può supportare:

- la **ricerca di un processo per PID** nella syscall `TERMPROCESS` (quando `targetPid ≠ 0`): i processi bloccati non si trovano in ready queue, quindi serve una struttura separata che li includa;
- il **rilevamento di deadlock** nello scheduler: è necessario sapere se esistono processi vivi non in ready queue e non soft-blocked, condizione non rilevabile tramite i soli contatori globali.

---

## 3. Modulo `scheduler.c` – Scheduling dei processi

### 3.1 Algoritmo di scheduling con priorità e round-robin

Lo scheduler preleva il processo con priorità più alta dalla ready queue tramite `removeProcQ`, che rispetta l'ordinamento per priorità. Ogni processo estratto riceve una time slice pari a `TIMESLICE × TIMESCALE` (vedi §6), caricata nel PLT tramite `setTIMER`. Allo scadere della slice (interrupt PLT), il processo corrente viene reinserito in coda e rischedulato.

Questa politica realizza un round-robin con priorità: processi ad alta priorità vengono sempre preferiti, ma a parità di priorità il tempo CPU è condiviso equamente.

### 3.2 Gestione degli stati del sistema

Lo scheduler distingue tre stati del sistema con azioni diverse:

- **Ready queue non vuota**: il primo processo viene estratto ed eseguito con `LDST`.
- **`processCount == 0`**: tutti i processi sono terminati, il sistema viene arrestato con `HALT`.
- **`softBlockCount > 0`**: esistono processi bloccati in attesa di interrupt. Il kernel entra in stato di attesa (`WAIT`) con tutti gli interrupt abilitati tranne il PLT (`MIE_ALL & ~MIE_MTIE_MASK`, vedi §6), per non consumare inutilmente cicli CPU e per non ricevere un interrupt PLT spurio mentre non c'è nessun processo in esecuzione.

### 3.3 Rilevamento del deadlock

Se la ready queue è vuota, ci sono processi vivi (`processCount > 0`) ma nessuno è soft-blocked (`softBlockCount == 0`), il sistema è in deadlock. Prima di chiamare `PANIC`, lo scheduler tenta di individuare un processo nell'array `activeProcs` (caso difensivo, per gestire eventuali inconsistenze nei contatori). Se nessun processo viene trovato, la chiamata a `PANIC` segnala esplicitamente la condizione anomala.

---

## 4. Modulo `exceptions.c` – Gestione delle eccezioni

### 4.1 Punto d'ingresso unificato (`exceptionHandler`)

Tutte le eccezioni hardware confluiscono in `exceptionHandler`, che legge il registro `cause` dalla pagina `BIOSDATAPAGE` e smista l'eccezione in base al codice:

- bit 31 attivo → interrupt (delegato a `interruptHandler`);
- `excCode` 8 o 11 → syscall (il PC viene avanzato di 4 prima dell'elaborazione);
- `excCode` 12, 13, 15 → TLB exception (`tlbExceptionHandler`);
- `excCode` 1, 5, 7 → gestione dipendente dal livello di privilegio corrente (MPP);
- qualsiasi altro codice → program trap (`programTrapHandler`).

L'avanzamento del PC di `WORDLEN` prima di invocare `syscallHandler` garantisce che, al ritorno dalla syscall, l'istruzione successiva venga eseguita correttamente senza rieseguire l'`ECALL`.

### 4.2 Aggiornamento del tempo CPU (`updateCPUTime`)

Ogni punto critico di eccezione aggiorna il tempo CPU del processo corrente calcolando il delta tra il TOD corrente e `startTOD`, poi aggiorna `startTOD`. Questo schema garantisce una contabilità precisa anche in presenza di eccezioni annidate o di più syscall consecutive, senza mai perdere frazioni di tempo.

### 4.3 Implementazione delle syscall

#### `CREATEPROCESS` (SYS1)

Alloca un PCB con `allocPcb`, lo configura con lo stato, la priorità e il puntatore alla struttura di supporto forniti dal chiamante, lo inserisce come figlio del processo corrente e lo aggiunge ad `activeProcs`. Dopo aver salvato lo stato aggiornato del padre (con `reg_a0 = PID` del figlio), entrambi i processi vengono inseriti in ready queue e si richiama lo scheduler, abilitando la preemption immediata se il figlio ha priorità superiore.

#### `TERMPROCESS` (SYS2)

Invoca la funzione ricorsiva `terminateProcess`, che tramite `removeChild` elimina prima tutti i discendenti (DFS), poi rimuove il processo dalla ASL (se bloccato), dalla ready queue e da `activeProcs`, liberando infine il PCB con `freePcb`. Il flag `p_pid = -1` durante la ricorsione evita terminazioni doppie in caso di riferimenti circolari.

#### `PASSEREN` / `VERHOGEN` (SYS3 / SYS4)

Implementano la semantica P/V con contatore negativo. In P, se il valore del semaforo scende sotto zero, il processo viene bloccato con `blockCurrentProcess` (che aggiorna `softBlockCount` solo per semafori di dispositivo reali, non per il pseudo-clock). In V, se il valore rimane ≤ 0, il primo processo in attesa viene rimosso dalla ASL e reinserito nella ready queue.


#### `DOIO` (SYS5)

Calcola l'indice del semaforo corrispondente al registro di comando indirizzato, scrive il comando sul registro hardware e, se il dispositivo non è già pronto, decrementa il semaforo e blocca il processo. Per i terminali vengono gestiti separatamente i sottocanali TX e RX tramite l'offset del registro all'interno del blocco dispositivo.

#### `CLOCKWAIT` (SYS7)

Decrementa il semaforo dello pseudo-clock e incrementa `softBlockCount` prima di bloccare il processo. Questo assicura che lo scheduler entri in stato `WAIT` se tutti i processi pronti si esauriscono prima del prossimo tick del timer (ogni `PSECOND`, vedi §6).

### 4.4 Meccanismo `passUpOrDie`

Quando un'eccezione non può essere gestita dal kernel (TLB miss, program trap), si applica la politica pass-up-or-die: se il processo ha una struttura di supporto, l'eccezione viene passata al gestore utente tramite `LDCXT`; altrimenti il processo (e tutta la sua discendenza) viene terminato e si richiama lo scheduler.

Questa scelta mantiene il kernel privo di dipendenze dai gestori utente, delegando la complessità delle eccezioni di livello superiore alla Phase 3.

### 4.5 `blockCurrentProcess` e `isDeviceSemaphore`

La funzione `blockCurrentProcess` centralizza la logica di blocco: aggiorna il tempo CPU, imposta `p_semAdd`, incrementa `softBlockCount` solo se il semaforo è un semaforo di dispositivo (verificato da `isDeviceSemaphore`, che esclude il semaforo dello pseudo-clock), inserisce il processo nella ASL e richiama lo scheduler.

Separare il controllo di appartenenza ai semafori di dispositivo in una funzione dedicata evita errori di conteggio in `softBlockCount`, che altrimenti porterebbe lo scheduler a eseguire `WAIT` in modo non corretto.

---

## 5. Modulo `interrupts.c` – Gestione degli interrupt

### 5.1 Struttura generale del gestore

`interruptHandler` legge `cause` da `BIOSDATAPAGE` e distingue tre classi di interrupt in base al codice di eccezione:

- `excCode` 7 → PLT (Process Local Timer): quantum scaduto;
- `excCode` 3 → Interval Timer: tick dello pseudo-clock (ogni `PSECOND`);
- `excCode` 17–21 → interrupt di dispositivo (linee hardware 3–7).

### 5.2 PLT interrupt e preemption

Allo scadere del time slice, il PLT viene disarmato con `setTIMER(NEVER)` (vedi §6) per evitare interrupt spuri durante la fase di scheduling. Lo stato del processo corrente viene salvato da `BIOSDATAPAGE` nel PCB e il processo viene reinserito nella ready queue con il bit `MIE` abilitato, garantendo che alla prossima esecuzione parta con gli interrupt attivi. Infine si richiama lo scheduler.

### 5.3 Interval Timer e pseudo-clock

All'arrivo del tick, il timer viene riarmato (`LDIT(PSECOND)`) e tutti i processi bloccati sul semaforo dello pseudo-clock vengono sbloccati in blocco: `removeBlocked` svuota la coda, ogni processo viene reinserito nella ready queue con `reg_a0 = 0` e `softBlockCount` decrementato. Il semaforo viene poi azzerato. Se esiste un processo corrente viene ripristinato con `LDST`, altrimenti si schedula.

### 5.4 Interrupt di dispositivo

Per ciascuna linea attiva (excCode 17–21 → linee hardware 3–7), si legge la bitmap degli interrupt pendenti e si seleziona il dispositivo con priorità più alta (bit meno significativo attivo). Lo status viene salvato prima di inviare l'ACK al dispositivo, per garantire che il processo sbloccato riceva il valore di status corretto in `reg_a0`.

Per la linea terminale (linea 7), TX e RX vengono gestiti separatamente con priorità TX > RX: questo evita la perdita di caratteri quando entrambi i sottocanali segnalano contemporaneamente.

Il semaforo viene incrementato solo se è negativo (cioè ci sono processi in attesa), evitando incrementi spurî che potrebbero destabilizzare la contabilità dei semafori contatore.

### 5.5 Ripristino del contesto

Al termine di ogni gestore di interrupt, se `currentProcess` è non NULL viene eseguito `LDST(savedState)` per riprendere il processo interrotto senza passare per lo scheduler. Solo se `currentProcess` è NULL (perché il processo era già terminato o bloccato) si richiama lo scheduler. Questa scelta minimizza il numero di context switch inutili.

---

## 6. Glossario delle costanti critiche

| Costante | Significato |
|---|---|
| `PSECOND` | Periodo dell'Interval Timer (100 ms in tick TOD). Determina la frequenza con cui i processi in `CLOCKWAIT` vengono sbloccati. |
| `TIMESLICE` | Durata della time slice assegnata a ciascun processo. Viene moltiplicata per `TIMESCALE` (fattore di scala del TOD) per ottenere i tick reali caricati nel PLT. |
| `NEVER` | Valore massimo di `cpu_t`. Caricato nel PLT per disarmarlo: con un timeout irraggiungibile il timer non scatta mai, evitando interrupt PLT spuri durante lo scheduling. |
| `MIE_ALL` | Maschera che abilita tutti i sorgenti di interrupt (PLT, Interval Timer, dispositivi). |
| `MIE_ALL & ~MIE_MTIE_MASK` | Come `MIE_ALL` ma con il bit MTIE (PLT) azzerato. Usato nel `WAIT` per ricevere interrupt di dispositivo e di timer senza ricevere un PLT spurio mentre nessun processo è in esecuzione. |

---

## 7. Considerazioni finali

Le scelte implementative della Phase 2 sono state orientate dai seguenti principi:

- **Separazione delle responsabilità**: ogni modulo gestisce un aspetto preciso del nucleo con interfacce minimali tra loro.
- **Contabilità accurata del tempo CPU**: l'aggiornamento di `startTOD` in ogni punto di eccezione garantisce che nessun ciclo CPU vada perso o conteggiato doppio.
- **Correttezza del `softBlockCount`**: il contatore viene aggiornato simmetricamente in ogni punto di blocco e sblocco, distinguendo i semafori di dispositivo reali dallo pseudo-clock.
- **Assenza di allocazioni dinamiche**: tutte le strutture dati sono statiche o preallocate dalla Phase 1, garantendo comportamento deterministico.
- **Delega verso l'alto con `passUpOrDie`**: il nucleo non gestisce eccezioni di competenza dei livelli superiori, ma le delega o termina il processo in modo sicuro.
- **Deviazioni documentate**: le scelte che si discostano dalla specifica (stack pointer del Pass Up Vector, caso speciale di `sem_testbinary`) sono motivate esplicitamente e circoscritte al minimo indispensabile.