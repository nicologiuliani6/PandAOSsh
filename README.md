# PandOSsh – Phase 1, Phase 2 & Phase 3

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

### Phase 3

La **Phase 3** implementa il **Support Level** (Level 4): memoria virtuale a paginazione e gestione delle U-proc in user-mode. Funzionalità implementate:

- **Inizializzazione e InstantiatorProcess** (`phase3/initProc.c`): pool delle Support Structure, lancio della shell (ASID 1), `masterSemaphore`/`shellSemaphore`, attesa e HALT finale.
- **Memoria virtuale / Pager** (`phase3/vmSupport.c`): Swap Pool (16 frame = 2·UPROCMAX), TLB exception handler con rimpiazzo pagine FIFO, lettura/scrittura del backing store (device flash), Page Table per U-proc.
- **Support Level syscall** (`phase3/sysSupport.c`): general exception handler, Program Trap handler e le syscall **SYS2** Terminate, **SYS4** WriteTerminal, **SYS5** ReadTerminal, **SYS6** Execute.
- **uTLB_RefillHandler** (`phase2/exceptions.c`, guardato da `SUPPORT_LEVEL`): ricarica nel TLB l'entry mancante dalla Page Table della U-proc corrente.

Ogni U-proc gira nello spazio `kuseg` (da `0x80000000`) con ASID univoco `[1..8]`, ed è caricata dal proprio device flash.

#### Programmi U-proc (`testers/`)

Una `shell` interattiva (ASID 1) legge un nome di programma dal terminale, lo traduce in ASID con mappatura statica e lo esegue via SYS6; `exit` termina la shell. Programmi disponibili:

| comando | ASID | flash |
|---------|------|-------|
| `shell` | 1 | flash0 |
| `fibEight` | 2 | flash1 |
| `echo` | 3 | flash2 |
| `fibEleven` | 4 | flash3 |
| `uname` | 5 | flash4 |
| `date` | 6 | flash5 |
| `sl` | 7 | flash6 |
| `calc` | 8 | flash7 |

---

## Emulatore µRISCV

Il progetto utilizza l'emulatore **µRISCV**, sviluppato dal gruppo *VirtualSquare*.

Il codice sorgente dell'emulatore, insieme alla documentazione e alle istruzioni di installazione, è disponibile nel repository ufficiale:

https://github.com/virtualsquare/uriscv

---

## Ambiente di sviluppo

All'interno della directory del progetto sono forniti:

- le specifiche ufficiali della Phase 1, Phase 2 e Phase 3 (in `documentazione/`);
- il materiale di supporto;
- i programmi di test per la verifica dell'implementazione (`p1test.c`, `p2test.c`) e i programmi U-proc della Phase 3 (`testers/`).

Il progetto è pensato per essere compilato ed eseguito in ambiente **GNU/Linux** (official support Debian/Fedora).

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

### Compilare e testare Phase 3

```bash
# 1. kernel del Support Level
cd build
make phase3

# 2. programmi U-proc (immagini flash)
cd ../testers
make
```

`make phase3` genera `build/MultiPandOS.core.uriscv`; `make` in `testers/` produce le immagini flash `*.uriscv`. Aprire **µRISC-V**, caricare **phase3_config_machine.json** e avviarlo. Per vedere l'output e digitare i comandi della shell andare su **Windows → Terminal 0**.

Esempio di sessione nel terminale:

```
PandOSsh shell
comandi: fibEight echo fibEleven uname date sl calc | exit
> calc
calc: inserisci <cifra><op><cifra> (es. 7*8): 7*8
calc: risultato = 56
> exit
shell: arrivederci
System halted
```

> **IMPORTANTE — file core condiviso:** Phase 1, Phase 2 e Phase 3 generano tutti lo stesso `build/MultiPandOS.core.uriscv`. Lanciare **sempre** `make phase3` (o `phase2`) **subito prima** di caricare la relativa configurazione, altrimenti l'emulatore esegue il kernel di un'altra fase. Le immagini flash in `testers/` sono ricostruibili con `make` (sono ignorate da git).


