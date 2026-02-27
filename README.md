# PandOSsh – Phase 1 & Phase 2

**Progetto di Laboratorio – A.A. 2025/2026**

## Introduzione

Questo progetto è stato sviluppato nell'ambito del corso di **Sistemi Operativi** per l'anno accademico **2025/2026**.  
L'obiettivo del progetto è l'implementazione di un sistema operativo didattico denominato **PandOSsh**.

Il progetto viene sviluppato utilizzando l'emulatore **µRISCV**, una macchina virtuale per architettura **RISC-V**.

---

## Descrizione del progetto

PandOSsh è un sistema operativo didattico progettato per essere eseguito su architettura **RISC-V** tramite l'emulatore **µRISCV**.

Il progetto è suddiviso in più fasi, ciascuna delle quale estende la precedente con nuove funzionalità del kernel.

### Phase 1

La **Phase 1** prevede l'implementazione delle principali strutture dati di base del kernel, in particolare:

- Process Control Block (PCB)
- Gestione delle code di processi
- Gestione dell'albero dei processi
- Active Semaphore List (ASL)

L'implementazione segue le specifiche fornite e non utilizza allocazioni dinamiche.

### Phase 2

La **Phase 2** prevede l'implementazione del **Nucleo** (Level 3) del sistema operativo, che si appoggia sulle strutture dati della Phase 1. Le funzionalità implementate sono:

- **Inizializzazione del Nucleo** (`initial.c`): configurazione del Pass Up Vector, inizializzazione delle strutture dati, creazione del processo iniziale di test.
- **Scheduler** (`scheduler.c`): scheduling round-robin preemptivo con time slice di 5ms tramite il Processor Local Timer (PLT).
- **Gestione delle eccezioni** (`exceptions.c`): handler per SYSCALL (NSYS1–NSYS10), TLB exception, Program Trap e meccanismo Pass Up or Die.
- **Gestione degli interrupt** (`interrupts.c`): handler per interrupt PLT, Interval Timer (Pseudo-clock tick ogni 100ms) e device esterni (disk, flash, ethernet, printer, terminal).

---

## Emulatore µRISCV

Il progetto utilizza l'emulatore **µRISCV**, sviluppato dal gruppo *VirtualSquare*.

Il codice sorgente dell'emulatore, insieme alla documentazione e alle istruzioni di installazione, è disponibile nel repository ufficiale:

https://github.com/virtualsquare/uriscv

---

## Ambiente di sviluppo

All'interno della directory del progetto sono forniti:

- le specifiche ufficiali della Phase 1 e Phase 2;
- il materiale di supporto;
- i programmi di test per la verifica dell'implementazione (`p1test.c`, `p2test.c`).

Il progetto è pensato per essere compilato ed eseguito in ambiente **GNU/Linux**.

---

## Compilazione

Il progetto utilizza **CMake** come sistema di build.

### Prima configurazione (una volta sola)

```bash
mkdir build
cd build
cmake ..
```

### Compilare e testare Phase 1

```bash
cd build
make phase1
```

Genera `build/MultiPandOS.core.uriscv`. Aprire **µRISC-V**, caricare **config_machine.json** e avviarlo. Per vedere l'output andare su **Windows → Terminal 0**.

### Compilare e testare Phase 2

```bash
cd build
make phase2
```

Genera `build/MultiPandOS.core.uriscv`. Aprire **µRISC-V**, caricare **config_machine.json** e avviarlo. Per vedere l'output andare su **Windows → Terminal 0**.

> **Nota:** entrambi i target sovrascrivono `MultiPandOS.core.uriscv` — il file caricato automaticamente da `config_machine.json` — quindi non è necessario modificare nulla nella configurazione dell'emulatore.

> **Importante (Phase 2):** assicurarsi che il campo **TLB Floor Address** in `config_machine.json` sia impostato a qualsiasi valore diverso da `VM_OFF`. Nel file fornito è già impostato a `0xffffffff`, quindi va bene così.

Il test ha successo se sul terminale compare il messaggio finale:
```
System halted
```cd
