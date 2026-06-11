# Scelte implementative – Phase 3
**Moduli `initProc.c` · `vmSupport.c` · `sysSupport.c` · `uTLB_RefillHandler` (in `phase2/exceptions.c`)**

---

## 1. Introduzione

Il presente documento descrive le principali **scelte implementative** adottate nei moduli della Phase 3 (Support Level, Level 4), motivandole in relazione ai requisiti del progetto e alle specifiche µRISCV. Dove il codice si discosta dalla specifica, la deviazione è segnalata esplicitamente e giustificata.

La Phase 3 realizza il livello di supporto che gira al di sopra del nucleo: memoria virtuale a paginazione, esecuzione delle U-proc in user-mode con ASID univoco, gestione dei page fault tramite un Pager con rimpiazzo FIFO e backing store su device flash, e le syscall di livello utente. Tutte le scelte mirano a garantire correttezza, mutua esclusione sulle risorse condivise e comportamento deterministico, senza alcuna allocazione dinamica.

---

## 2. Modulo `initProc.c` – Inizializzazione del Support Level

### 2.1 InstantiatorProcess e shell unica

A differenza dello schema tradizionale che lancia direttamente le 8 U-proc, `test` avvia una **sola U-proc** (la `shell`, ASID 1). Sono le syscall **SYS6 Execute** della shell a creare a runtime le altre U-proc, una alla volta. Questa scelta trasforma il Support Level in un sistema interattivo: l'utente digita il nome di un programma e la shell lo esegue, attendendone la terminazione prima di tornare al prompt.

### 2.2 Support structure pool statico

Le strutture di supporto sono preallocate in un array statico `supportPool[UPROCMAX]`, indicizzato direttamente dall'ASID (`getSupport(asid) = &supportPool[asid-1]`). Questo elimina ogni allocazione dinamica e garantisce un legame uno-a-uno tra ASID e support structure, semplice e deterministico.

### 2.3 Semafori di sincronizzazione

Si usano due semafori binari distinti:

- `masterSemaphore`: l'InstantiatorProcess vi si blocca dopo aver avviato la shell; viene rilasciato solo quando la shell (ASID 1) termina, permettendo l'HALT finale del sistema.
- `shellSemaphore`: la shell vi si blocca durante una SYS6, in attesa che la U-proc figlia appena lanciata termini.

Separare i due semafori rende la catena di attesa esplicita: `figlia → shell → InstantiatorProcess`. La distinzione tra i due viene fatta in fase di terminazione in base all'ASID (vedi §4.1).

### 2.4 Mutex sui device

L'array `devMutex[DEV_MUTEX_TOTAL]` fornisce mutua esclusione per dispositivo, inizializzato a 1 (semaforo binario). È necessario perché più U-proc possono contendersi lo stesso device flash (durante il paging) o il terminale 0 (condiviso da tutti i programmi e dalla shell).

### 2.5 Configuranzione degli exception context

In `launchUproc` ciascuna U-proc riceve due exception context, uno per i page fault (`PGFAULTEXCEPT → pager`) e uno per le altre eccezioni (`GENERALEXCEPT → generalExceptionHandler`), con stack separati (`sup_stackTLB`, `sup_stackGen`) interni alla support structure. Stack distinti evitano che un page fault occorso durante la gestione di una syscall corrompa lo stack dell'altro handler.

### 2.6 Stato iniziale della U-proc

Lo stato di partenza imposta `pc_epc = UPROCSTARTADDR` (0x800000B0, inizio del `.text` dopo l'header aout), `reg_sp = USERSTACKTOP` (0xC0000000), user-mode con interrupt e PLT abilitati, ed `entry_hi` con l'ASID univoco. La U-proc viene poi materializzata dal Nucleus con `CREATEPROCESS` (NSYS1), passando il puntatore alla support structure.

---

## 3. Modulo `vmSupport.c` – Memoria virtuale e Pager

### 3.1 Swap Pool e semaforo di mutua esclusione

Lo Swap Pool è un array statico di `SWAP_POOL_SIZE` (16 = 2·UPROCMAX) frame fisici, ciascuno descritto da una `swap_t` (ASID, numero di pagina, puntatore alla PTE). Il semaforo binario `swapPoolSem` protegge l'intera tabella: il Pager lo acquisisce all'inizio e lo rilascia prima di ogni uscita, garantendo che un solo page fault per volta manipoli i frame e il backing store.

### 3.2 Page Table privata e mappatura VPN→indice

Ogni U-proc ha una Page Table di 32 entry: le pagine 0..30 mappano il `.text`/`.data` a partire da `KUSEG_VPN_START`, l'indice 31 è la pagina di stack (VPN `0xBFFFF`). La funzione `vpnToIndex` traduce l'EntryHI in indice gestendo separatamente il caso dello stack; un VPN fuori range restituisce −1 e il Pager termina la U-proc come program trap. Inizialmente tutte le entry hanno `V=0` (non presenti): la prima referenza genera un page fault.

### 3.3 Rimpiazzo pagine FIFO

La vittima viene scelta con un semplice **round-robin** (`fifoNext = (fifoNext+1) % SWAP_POOL_SIZE`). FIFO è preferito ad algoritmi più sofisticati (LRU, second-chance) perché la specifica lo ammette esplicitamente ed è O(1) senza strutture aggiuntive, in linea con il principio di determinismo del progetto.

### 3.4 Aggiornamento atomico di Page Table + TLB

> **Scelta progettuale: installazione diretta in TLB**

Le funzioni `markPageNotValid` e `markPagePresent` aggiornano la PTE e il TLB con gli interrupt disabilitati, per rendere atomica la coppia di operazioni. La scelta critica è in `markPagePresent`: oltre a `TLBCLR` (che azzera le entry stantie), l'entry appena resa valida viene scritta **direttamente** nel TLB con `setENTRYHI`/`setENTRYLO`/`TLBWR`.

Questa deviazione dallo schema "solo TLBCLR" è stata adottata dopo aver diagnosticato un **page-fault loop**: in alcune situazioni l'evento di TLB-Refill smetteva di rigenerare l'entry e i fault venivano dirottati sul Pager, che con il solo `TLBCLR` non installava mai la traduzione, lasciando la U-proc a ripetere all'infinito lo stesso fault. Installando la traduzione direttamente nel TLB, l'accesso che riprende subito dopo trova già l'entry valida.

### 3.5 Backing store su device flash

Il backing store di ciascuna U-proc è il device flash con `devNo = asid − 1`. `flashOperation` acquisisce il mutex del device, imposta `data0` con l'indirizzo del frame (DMA), compone il comando (numero blocco nei 3 byte alti, opcode nel byte basso) e lo emette con `DOIO`. La mutua esclusione per-device è separata da `swapPoolSem` per non serializzare inutilmente operazioni su flash diversi.

### 3.6 Sequenza del Pager

Il Pager: recupera la support structure (`GETSUPPORTPTR`); tratta `EXC_MOD` come program trap; acquisisce `swapPoolSem`; mappa il VPN in indice; sceglie il frame FIFO; se occupato sfratta la vittima (invalida PTE+TLB, scrive il frame sul suo backing store); legge la pagina richiesta dal flash; aggiorna la Swap Pool table; rende presente la PTE (con installazione in TLB); rilascia `swapPoolSem`; riprende la U-proc con `LDST`. Ogni errore di I/O sul flash comporta la terminazione ordinata della U-proc.

---

## 4. Modulo `sysSupport.c` – Syscall ed eccezioni del Support Level

### 4.1 Terminazione ordinata (`supTerminate`)

Prima di terminare una U-proc tramite il Nucleus (NSYS2), `supTerminate` **libera i frame** dello Swap Pool occupati da quell'ASID (sotto `swapPoolSem`), evitando che un futuro sfratto scriva pagine ormai morte sul backing store. Poi sblocca il giusto attendente in base all'ASID: la shell (ASID 1) rilascia `masterSemaphore`; una U-proc figlia rilascia `shellSemaphore`. Questo è l'unico punto in cui la catena di attesa descritta in §2.3 viene sciolta.

### 4.2 SYS4 WriteTerminal e SYS5 ReadTerminal

Entrambe validano l'indirizzo virtuale (dentro `[KUSEG, USERSTACKTOP)`) e terminano la U-proc se la richiesta è malformata, secondo la politica "input non valido = program trap". L'accesso al terminale è serializzato da mutex TX/RX distinti (`TERMW_MUTEX`/`TERMR_MUTEX`): TX e RX del terminale sono sottocanali indipendenti, separarli evita falsi conflitti tra scrittura e lettura. La lettura accumula caratteri fino al `\n`; la scrittura interrompe e segnala l'errore se lo status di trasmissione non è `OKCHARTRANS`.

### 4.3 SYS6 Execute

`doExecute` valida l'ASID `[1..UPROCMAX]`, lancia la U-proc figlia con `launchUproc` e **blocca la shell** su `shellSemaphore` finché la figlia non termina. È questo blocco a rendere la shell sincrona: un programma per volta, prompt restituito solo a esecuzione conclusa.

### 4.4 General Exception Handler e dispatch

`generalExceptionHandler` recupera la support structure e legge il `cause`: se è una `ECALL` da user-mode (`EXC_ECU`) la inoltra al `supSyscallHandler`; qualsiasi altra eccezione è un program trap e termina la U-proc. Il dispatcher delle syscall, al ritorno, scrive il risultato in `a0` e avanza `pc_epc` di `WORDLEN` per non rieseguire la `ECALL`. Le syscall non riconosciute sono trattate come program trap.

---

## 5. `uTLB_RefillHandler` (in `phase2/exceptions.c`)

Il gestore di TLB-Refill è il percorso veloce per i TLB miss: legge l'indice di pagina dall'EntryHI, preleva la entry dalla Page Table privata della U-proc corrente (`currentProcess->p_supportStruct->sup_privatePgTbl`) e la installa nel TLB con `setENTRYHI`/`setENTRYLO`/`TLBWR`, poi riprende con `LDST`. È compilato condizionalmente (`SUPPORT_LEVEL`): nella Phase 2 pura il refill non esiste, perché non c'è memoria virtuale di livello utente. La sua collocazione in `phase2/exceptions.c` rispetta la responsabilità del Nucleus sul Pass Up Vector, pur servendo strutture dati definite dalla Phase 3.

---

## 6. Glossario delle costanti critiche

| Costante | Significato |
|---|---|
| `UPROCMAX` | Numero massimo di U-proc (8); coincide con il numero di ASID utente `[1..8]` e di device flash. |
| `SWAP_POOL_SIZE` | Frame fisici dello Swap Pool (16 = 2·UPROCMAX). Dimensiona la tabella e il modulo del round-robin FIFO. |
| `SWAP_FRAME_FREE` | Valore di `sw_asid` che marca un frame dello Swap Pool come libero. |
| `KUSEG_VPN_START` | VPN iniziale dello spazio logico utente (segmento `kuseg`, da `0x80000000`). |
| `KUSEG_STACK_VPN` | VPN della pagina di stack (`0xBFFFF`), mappata all'indice 31 della Page Table. |
| `UPROCSTARTADDR` | Indirizzo di ingresso del `.text` della U-proc (`0x800000B0`), dopo l'header aout. |
| `USERSTACKTOP` | Cima dello stack utente (`0xC0000000`). |
| `DIRTYON` / `VALIDON` | Bit D (scrivibile) e V (presente) di un EntryLO. |

---

## 7. Considerazioni finali

Le scelte implementative della Phase 3 sono state orientate dai seguenti principi:

- **Modello interattivo a shell unica**: una sola U-proc lanciata direttamente, le altre create a runtime via SYS6, con catena di attesa esplicita tra figlia, shell e InstantiatorProcess.
- **Mutua esclusione granulare**: `swapPoolSem` per lo Swap Pool, mutex per-device per flash e terminale, evitando serializzazioni superflue.
- **Atomicità Page Table ↔ TLB**: ogni modifica di una PTE è accompagnata dal corrispondente aggiornamento del TLB a interrupt disabilitati.
- **Terminazione ordinata**: prima di morire, una U-proc libera i propri frame di Swap Pool e sblocca l'attendente corretto, mantenendo coerenti tabelle e semafori.
- **Assenza di allocazioni dinamiche**: support pool, Swap Pool e Page Table sono tutte strutture statiche, garantendo comportamento deterministico.
- **Deviazioni documentate**: l'installazione diretta in TLB nel Pager (correttivo per il page-fault loop) è motivata esplicitamente e circoscritta al minimo indispensabile.
